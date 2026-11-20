/**
 * @file    user_link.c
 * @brief   指令链路实现 · USART1 + 中断接收 + LVGL 定时器轮询
 */
#include "user_link.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "main.h"
#include "usart.h"      /* huart1 */
#include "ui_main.h"
#include "user_clock.h"
#include "ui_rec.h"
#include "ui_history.h"      /* 录音页：显示识别文字 */

#define LINK_LINE_MAX   96
#define LINK_POLL_MS    30

/* ── 蓝牙（HC-05）第二通道 ────────────────────────────────────
 * 接线：HC-05 TXD → PA3（USART2_RX）
 *       HC-05 RXD → PA2（USART2_TX，飞线）
 *       VCC → 3V3、GND → GND、EN/KEY 悬空、STATE 不接
 *
 * 波特率：HC-05 出厂是 9600 8N1（**不是** USART1 的 115200）。
 * 想改成 115200 得先让它进 AT 模式（EN 拉高再上电，38400 发 AT+BAUD8），
 * 改完只需把下面的 BT_BAUD 跟着改，其余代码一行都不用动。
 */
#define BT_BAUD         9600u
#define BT_TX_TIMEOUT   100

/* 发出去的数据同时送到 USB 串口和蓝牙：
 * 网关接在哪一路都能用，不用在固件里选。 */

/* ── 接收侧（中断里只写这些）───────────────────────────── */
static uint8_t s_rx_byte;
static char    s_line[LINK_LINE_MAX];
static volatile uint16_t s_len = 0;

/* 中断 → 主循环 的一格信箱：够用且绝不阻塞 */
static volatile bool s_ar_ready = false;   /* 收到一条 AR */
static volatile bool s_ar_ok    = false;   /* 该 AR 是否 OK */

/* 授时信箱：ISR 里只存数据，真正写 RTC 交给定时器 */
static volatile bool     s_time_ready = false;
static volatile uint16_t s_time_h = 0, s_time_m = 0, s_time_s = 0;
static volatile uint16_t s_time_y = 0, s_time_mon = 0, s_time_day = 0;

/* 回执文字信箱：网关回来的 "音量 77%""已切到下一首" 这类短语 */
static volatile bool s_reply_ready = false;
static char s_reply_buf[64];

/* 语音识别结果信箱：ISR 里只存文字，界面刷新交给定时器 */
static volatile bool s_asr_ready = false;
static char s_asr_buf[96];

/* 心跳应答信箱：收到 PING 就置位，由定时器回 PONG。
 * ⚠️ 为什么不在中断里直接回：9600 波特率发 9 个字节要 9ms，
 *    放在中断里会长时间阻塞，可能拖垮 FreeRTOS 的节拍。 */
static volatile bool s_pong_pending = false;

/* ── 发送侧 ───────────────────────────────────────────── */
static uint8_t    s_req_id  = 0;
static ui_icon_t  s_pending = UI_ICON_MUSIC;   /* 正在等回执的意图 */

/* ── 蓝牙通道（USART2）状态 ───────────────────────────── */
static UART_HandleTypeDef s_huart_bt;
static uint8_t  s_bt_rx_byte;
static char     s_bt_line[LINK_LINE_MAX];
static volatile uint16_t s_bt_len = 0;
static bool     s_bt_ready = false;
static volatile bool s_bt_first_frame = false;   /* 收到第一帧时在 USB 上提示一次 */

/** 往 USB 串口打一行调试信息（设备启动日志走这里） */
static void link_dbg(const char *s)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)s, (uint16_t)strlen(s), 100);
}

/** 把一帧同时发到 USB 串口和蓝牙（谁连着网关谁就能收到） */
static void link_send_frame(const char *buf, int n)
{
    if (n <= 0) return;
    HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)n, BT_TX_TIMEOUT);
    if (s_bt_ready) {
        HAL_UART_Transmit(&s_huart_bt, (uint8_t *)buf, (uint16_t)n, BT_TX_TIMEOUT);
    }
}

/** 初始化蓝牙通道：USART2 @ 9600，PA2=TX / PA3=RX（都是 AF7） */
static void bt_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Pin       = GPIO_PIN_2 | GPIO_PIN_3;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_PULLUP;          /* 模块没接时 RX 不会乱捡电平 */
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &g);

    s_huart_bt.Instance          = USART2;
    s_huart_bt.Init.BaudRate     = BT_BAUD;
    s_huart_bt.Init.WordLength   = UART_WORDLENGTH_8B;
    s_huart_bt.Init.StopBits     = UART_STOPBITS_1;
    s_huart_bt.Init.Parity       = UART_PARITY_NONE;
    s_huart_bt.Init.Mode         = UART_MODE_TX_RX;
    s_huart_bt.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    s_huart_bt.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&s_huart_bt) != HAL_OK) {
        link_dbg("[bt] USART2 初始化失败\r\n");
        return;
    }

    HAL_UART_Receive_IT(&s_huart_bt, &s_bt_rx_byte, 1);
    HAL_NVIC_SetPriority(USART2_IRQn, 5, 1);
    HAL_NVIC_EnableIRQ(USART2_IRQn);

    s_bt_ready = true;
    link_dbg("[bt] USART2 ready @9600 (PA2=TX, PA3=RX)\r\n");
}

/* ═══════════════════════ 工具 ═══════════════════════ */

const char *link_code_of(ui_icon_t kind)
{
    switch (kind) {
    case UI_ICON_MUSIC:  return "A01";   /* 放歌/暂停 */
    case UI_ICON_NEXT:   return "A02";   /* 下一首 */
    case UI_ICON_MOON:   return "A03";   /* 我困了：锁屏+静音+关屏 */
    case UI_ICON_VOL:    return "A04";   /* 音量+ */
    case UI_ICON_MUTE:   return "A05";   /* 音量- */
    case UI_ICON_FOLDER: return "A06";   /* 打开资料 */
    case UI_ICON_LIST:   return "A07";   /* 查状态 */
    case UI_ICON_GRID:   return "A07";   /* 兼容：老的主界面「更多」格 */
    case UI_ICON_MIC:    return "A08";   /* 语音（待接） */
    default:             return "A07";
    }
}

/* ═══════════════════════ 发送 ═══════════════════════ */

void link_send_intent(ui_icon_t kind)
{
    char buf[48];
    s_req_id = (uint8_t)((s_req_id + 1) & 0xFF);
    s_pending = kind;

    int n = snprintf(buf, sizeof(buf), "OV+AI=%u,%s\r\n",
                     (unsigned)s_req_id, link_code_of(kind));
    if (n > 0) {
        link_send_frame(buf, n);
    }
}

void link_send_text(const char *text)
{
    char buf[128];
    if (!text || text[0] == '\0') return;

    s_req_id = (uint8_t)((s_req_id + 1) & 0xFF);
    s_pending = UI_ICON_MIC;      /* 回执时在界面显示麦克风图标 */

    /* T = 自由文本通道；内容由网关的关键词/LLM 映射成白名单动作 */
    int n = snprintf(buf, sizeof(buf), "OV+AI=%u,T,%s\r\n",
                     (unsigned)s_req_id, text);
    if (n > 0) {
        link_send_frame(buf, n);
    }
}
/** 通知网关开始/结束录音（电脑麦克风兜底方案用）
 *  为什么要分 start/stop：用户说的话在按开始与按停止之间，
 *  等停止才录音就录不到人声了。*/
void link_send_mic_ctrl(bool start)
{
    char buf[48];
    s_req_id = (uint8_t)((s_req_id + 1) & 0xFF);
    s_pending = UI_ICON_MIC;
    int n = snprintf(buf, sizeof(buf), "OV+AI=%u,A08,%s\r\n",
                     (unsigned)s_req_id, start ? "start" : "stop");
    if (n > 0) {
        link_send_frame(buf, n);
    }
}

/* ═══════════════════════ 接收 ═══════════════════════ */

/**
 * 解析一条完整行。
 * 认识两种帧：
 *   OV+AR=<id>,<STATUS>,<TEXT>            指令回执
 *   OV+EV=TIME,<HH:MM:SS>,<YYYY-MM-DD>    PC 授时（用于校正 RTC）
 */
static void link_parse_line(char *line)
{
    if (strncmp(line, "OV+AR=", 6) == 0) {
        unsigned id = 0;
        char status[8] = {0};
        if (sscanf(line + 6, "%u,%7[^,]", &id, status) < 2) {
            return;
        }
        /* 中断上下文只置标志，UI 更新交给定时器 */
        s_ar_ok    = (strcmp(status, "OK") == 0);
        s_ar_ready = true;

        /* 第三个字段是回执文字（可能没有）。格式：OV+AR=<id>,<STATUS>,<TEXT>
           不用 sscanf("%[^,]") 取——文字里可能含逗号，
           定位到第二个逗号之后整段收下更稳。 */
        {
            const char *p = strchr(line + 6, ',');
            if (p) p = strchr(p + 1, ',');
            if (p) {
                p++;
                size_t n = 0;
                while (p[n] != '\0' && p[n] != '\r' && p[n] != '\n'
                       && n < sizeof(s_reply_buf) - 1) {
                    n++;
                }
                memcpy(s_reply_buf, p, n);
                s_reply_buf[n] = '\0';
                s_reply_ready  = true;
            }
        }
        return;
    }

    if (strncmp(line, "OV+EV=TIME,", 11) == 0) {
        unsigned h = 0, mi = 0, sec = 0, y = 0, mon = 0, day = 0;
        if (sscanf(line + 11, "%u:%u:%u,%u-%u-%u",
                   &h, &mi, &sec, &y, &mon, &day) == 6) {
            /* RTC 的写操作要等同步标志，耗时较长，不能在中断里做，
               所以只把数据放进信箱，由 30ms 定时器真正写入 */
            s_time_h = h; s_time_m = mi; s_time_s = sec;
            s_time_y = y; s_time_mon = mon; s_time_day = day;
            s_time_ready = true;
        }
        return;
    }

    /* 心跳：网关每 20 秒发一次 OV+PING，必须回 PONG。
     * ⚠️ 以前这里漏了这条，网关每 60 秒就判一次"掉线"并清空去重表。 */
    if (strncmp(line, "OV+PING", 7) == 0) {
        s_pong_pending = true;         /* 实际发送交给定时器，中断里不阻塞 */
        return;
    }

    if (strncmp(line, "OV+EV=ASR,", 10) == 0) {        const char *p = line + 10;
        size_t i = 0;
        while (p[i] != '\0' && i < sizeof(s_asr_buf) - 1) {
            s_asr_buf[i] = p[i];
            i++;
        }
        s_asr_buf[i] = '\0';
        s_asr_ready = true;
        return;
    }

    /* 其它帧（PING/PONG 等）忽略 */
}

/** USART1 单字节接收完成中断（HAL 的 __weak 回调，这里覆盖它）
 *  同一套协议也跑在 USART2（蓝牙）上，两路各自攒行、共用解析。 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        char c = (char)s_rx_byte;
        if (c == '\n') {
            if (s_len > 0) {
                s_line[s_len] = '\0';
                link_parse_line(s_line);
                s_len = 0;
            }
        } else if (c != '\r') {
            if (s_len < LINK_LINE_MAX - 1) {
                s_line[s_len++] = c;
            } else {
                s_len = 0;             /* 超长行直接丢弃，防缓冲区被撑坏 */
            }
        }
        HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1);
    } else if (huart->Instance == USART2) {
        char c = (char)s_bt_rx_byte;

        /* 收到蓝牙第一帧时在 USB 上提示一次：说明蓝牙链路真的通了 */
        if (!s_bt_first_frame) {
            s_bt_first_frame = true;
            link_dbg("[bt] 收到第一帧，蓝牙链路已通\r\n");
        }

        if (c == '\n') {
            if (s_bt_len > 0) {
                s_bt_line[s_bt_len] = '\0';
                link_parse_line(s_bt_line);
                s_bt_len = 0;
            }
        } else if (c != '\r') {
            if (s_bt_len < LINK_LINE_MAX - 1) {
                s_bt_line[s_bt_len++] = c;
            } else {
                s_bt_len = 0;
            }
        }
        HAL_UART_Receive_IT(&s_huart_bt, &s_bt_rx_byte, 1);
    }
}

/* ═══════════════════════ LVGL 定时器轮询 ═══════════════════════ */

/**
 * USART1 中断入口
 * ⚠️ CubeMX 生成的 stm32f4xx_it.c 里没有它（.ioc 里没勾 USART1 中断），
 *    而它在本工程中没有重复定义，所以放在这里最干净——不动生成文件。
 */
void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}

/** USART2（蓝牙）中断入口 */
void USART2_IRQHandler(void)
{
    HAL_UART_IRQHandler(&s_huart_bt);
}

/** 在 LVGL 任务上下文里跑，所以可以安全地动控件 */
static void link_poll_timer(lv_timer_t *t)
{
    (void)t;

    /* ① PC 授时到达 → 写入 RTC（写 RTC 要等同步标志，不能放中断里） */
    if (s_time_ready) {
        s_time_ready = false;
        clock_set((uint16_t)s_time_y, (uint8_t)s_time_mon, (uint8_t)s_time_day,
                  (uint8_t)s_time_h, (uint8_t)s_time_m, (uint8_t)s_time_s);
    }

    /* ② 识别结果到达 → 显示到录音页 */
    if (s_asr_ready) {
        s_asr_ready = false;
        ui_rec_set_text(s_asr_buf);
        ui_history_add(s_asr_buf);   /* 同时记入语音历史 */
    }

    /* ②b 回执文字到达 → 显示在回执横条中间 */
    if (s_reply_ready) {
        s_reply_ready = false;
        ui_main_set_reply(s_reply_buf);
    }

    /* ③ 指令回执到达 → 更新回执横条与抽屉圆点 */
    if (s_ar_ready) {
        s_ar_ready = false;
        ui_main_set_result(s_pending, s_ar_ok);
        ui_history_mark_last(s_ar_ok);   /* 历史里那条的结果 */
    }

    /* ④ 收到过 PING → 在这里回 PONG（两路都发，谁连着网关谁收到） */
    if (s_pong_pending) {
        s_pong_pending = false;
        link_send_frame("OV+PONG\r\n", 9);
    }
}

/* ═══════════════════════ 意图回调（UI → 链路）═══════════════════════ */

static void on_ui_mic(bool start)
{
    link_send_mic_ctrl(start);
}

static void on_ui_text(const char *text)
{
    link_send_text(text);
}

static void on_ui_intent(ui_icon_t kind)
{
    link_send_intent(kind);
}

/* ═══════════════════════ 初始化 ═══════════════════════ */

void link_init(void)
{
    s_len = 0;
    s_ar_ready = false;

    /* UI 点意图时回调到这里（UI 本身不需要知道串口的存在） */
    ui_main_set_intent_cb(on_ui_intent);
    ui_main_set_text_cb(on_ui_text);   /* 录音页确认发送 */
    ui_main_set_mic_cb(on_ui_mic);     /* 录音开始/结束 */

    /* 启动中断接收 */
    HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1);

    /* 蓝牙通道（USART2 @9600）。初始化失败也不影响 USB 那条链路 */
    bt_init();

    /* 使能 USART1 中断（CubeMX 没开，我们自己开）
     * 优先级 5：不高于 configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY，
     * 这样 ISR 里就算以后要调 FreeRTOS 的 FromISR 接口也安全 */
    HAL_NVIC_SetPriority(USART1_IRQn, 5, 1);
    HAL_NVIC_EnableIRQ(USART1_IRQn);

    /* 轮询回执，30ms 一次 */
    lv_timer_create(link_poll_timer, LINK_POLL_MS, NULL);
}
