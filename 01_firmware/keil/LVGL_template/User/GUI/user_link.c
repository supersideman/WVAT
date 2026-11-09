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

/* 语音识别结果信箱：ISR 里只存文字，界面刷新交给定时器 */
static volatile bool s_asr_ready = false;
static char s_asr_buf[96];

/* ── 发送侧 ───────────────────────────────────────────── */
static uint8_t    s_req_id  = 0;
static ui_icon_t  s_pending = UI_ICON_MUSIC;   /* 正在等回执的意图 */

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
        HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)n, 100);
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
        HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)n, 100);
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
        HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)n, 100);
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

    if (strncmp(line, "OV+EV=ASR,", 10) == 0) {
        const char *p = line + 10;
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

/** USART1 单字节接收完成中断（HAL 的 __weak 回调，这里覆盖它） */
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

    /* ③ 指令回执到达 → 更新回执横条与抽屉圆点 */
    if (s_ar_ready) {
        s_ar_ready = false;
        ui_main_set_result(s_pending, s_ar_ok);
        ui_history_mark_last(s_ar_ok);   /* 历史里那条的结果 */
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

    /* 使能 USART1 中断（CubeMX 没开，我们自己开）
     * 优先级 5：不高于 configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY，
     * 这样 ISR 里就算以后要调 FreeRTOS 的 FromISR 接口也安全 */
    HAL_NVIC_SetPriority(USART1_IRQn, 5, 1);
    HAL_NVIC_EnableIRQ(USART1_IRQn);

    /* 轮询回执，30ms 一次 */
    lv_timer_create(link_poll_timer, LINK_POLL_MS, NULL);
}
