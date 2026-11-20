/**
 * @file    user_i2s.c
 * @brief   I2S2 数字麦克风采集 —— 寄存器级实现 + 两套引脚组合自动探测
 *
 * 为什么不用 HAL 的 I2S：
 *   ① 模板工程里根本没有 stm32f4xx_hal_i2s.c（和当初 RTC 一样缺文件）；
 *   ② HAL 的轮询接收有 bug：源码里是
 *          (*pRxBuffPtr) = (uint16_t)hi2s->Instance->DR;
 *      INMP441 的 24bit 数据在 32bit 时隙里**高位对齐**，取低 16 位只剩最低 8 位。
 *      所以这里直接读 32 位的 DR。
 *
 * I2S2 的可用引脚（F411 复用表，**全部是 AF5**）：
 *   主用：WS=PB12  CK=PB13  SD=PB15   ← 核心板右排最上面两行
 *   备用：WS=PB9   CK=PB10  SD=PC3    ← CK/SD 恰好在扩展板 2×9 上
 *
 * ⚠️ AF 号必须是 5：HAL 头文件里 GPIO_AF5_SPI2 才注释成
 *    "SPI2/I2S2 Alternate Function mapping"，GPIO_AF6_I2S2ext 是 I2S2ext_SD。
 *
 * 为什么让固件自动探测：
 *   AF5/AF6 具体哪套生效，文档之间有出入；与其让人反复烧板试，
 *   不如开机两套都试一遍，谁能收到数据就用谁，并把结论打到串口。
 *
 * 时钟（本板 25MHz HSE、PLLM=12 → PLL 输入 2.0833MHz）：
 *   PLLI2S N=58, R=2 → I2S 时钟 60.417MHz
 *   16kHz × 64bit/帧 = 1.024MHz → I2SDIV=29 + ODD=1
 *   实际采样率 16000.2Hz（误差 0.001%）
 */
#include "user_i2s.h"

#include <stdio.h>
#include <string.h>

#include "main.h"
#include "usart.h"

/* ── 时钟参数 ── */
#define MIC_PLLI2S_N 58u
#define MIC_PLLI2S_R 2u
#define MIC_I2SDIV   29u
#define MIC_I2SODD   1u

/* 探测：一次读 256 个 32bit 字 = 128 帧 = 8ms；150ms 没数据就判失败 */
#define MIC_PROBE_WORDS 256u
#define MIC_PROBE_TIMEOUT_MS 150u

/* 自检：512 字 = 256 帧 = 16ms/块，63 块 ≈ 1 秒 */
#define MIC_CHUNK_WORDS 512u
#define MIC_CHUNKS      63u

/**
 * 一套 I2S2 引脚配置。
 * AF 是**按引脚**独立的：主用引脚是 AF5、备用引脚是 AF6，
 * 所以允许 WS 用主用脚、CK/SD 用备用脚这种混合接法。
 */
typedef struct {
    GPIO_TypeDef *ws_port; uint16_t ws_pin; uint8_t ws_af;
    GPIO_TypeDef *ck_port; uint16_t ck_pin; uint8_t ck_af;
    GPIO_TypeDef *sd_port; uint16_t sd_pin; uint8_t sd_af;
    const char   *name;
} mic_pinset_t;

/* 候选组合，开机依次探测，谁有数据用谁（顺序 = 优先级）
 *
 * 三套的复用号**全是 AF5**：F411 上 SPI2/I2S2 就是 AF5。
 * 早期版本把 ②③④ 写成 AF6 是错的（AF6 那几个脚是 I2S2ext_SD / SPI3），
 * 结果 SCK 上没有波形，四套组合全部"没数据"。
 */
static const mic_pinset_t SET_LIST[] = {
    /* ① 全主用（三根都飞线到 PB12/13/15 时命中）*/
    { GPIOB, GPIO_PIN_12, GPIO_AF5_SPI2,
      GPIOB, GPIO_PIN_13, GPIO_AF5_SPI2,
      GPIOB, GPIO_PIN_15, GPIO_AF5_SPI2, "PB12/PB13/PB15 全 AF5" },

    /* ② 混合：WS 飞 PB12，CK/SD 走扩展板 2×9 的 PB10/PC3（最省焊的方案）*/
    { GPIOB, GPIO_PIN_12, GPIO_AF5_SPI2,
      GPIOB, GPIO_PIN_10, GPIO_AF5_SPI2,
      GPIOC, GPIO_PIN_3,  GPIO_AF5_SPI2, "PB12 + PB10 + PC3 混合" },

    /* ③ WS 飞 PB9（在主排针上，2×9 没有），CK/SD 走 2×9 */
    { GPIOB, GPIO_PIN_9,  GPIO_AF5_SPI2,
      GPIOB, GPIO_PIN_10, GPIO_AF5_SPI2,
      GPIOC, GPIO_PIN_3,  GPIO_AF5_SPI2, "PB9 + PB10 + PC3 全 AF5" },
};

#define MIC_SET_COUNT (sizeof(SET_LIST) / sizeof(SET_LIST[0]))

static bool     s_ready = false;
static uint32_t s_last_peak = 0;
static uint32_t s_buf[MIC_CHUNK_WORDS];
static const mic_pinset_t *s_active = NULL;

/* ───────────────── 工具 ───────────────── */

static void mic_print(const char *s)
{
    uint16_t n = (uint16_t)strlen(s);
    if (n) HAL_UART_Transmit(&huart1, (uint8_t *)s, n, 100);
}

static uint32_t isqrt_u64(uint64_t v)
{
    uint64_t rem = 0, root = 0;
    for (int i = 31; i >= 0; i--) {
        root <<= 1;
        rem = (rem << 2) | ((v >> (i * 2)) & 3u);
        if (root < rem) {
            rem -= root + 1;
            root += 2;
        }
    }
    return (uint32_t)(root >> 1);
}

/* ───────────────── 底层配置 ───────────────── */

static bool mic_pll2_config(void)
{
    RCC->CR &= ~RCC_CR_PLLI2SON;
    while (RCC->CR & RCC_CR_PLLI2SON) { }

    uint32_t v = 0;
    v |= (MIC_PLLI2S_N & 0x1FFu) << 6;
    v |= (MIC_PLLI2S_R & 0x7u) << 28;
    RCC->PLLI2SCFGR = v;

    RCC->CR |= RCC_CR_PLLI2SON;
    uint32_t t0 = HAL_GetTick();
    while ((RCC->CR & RCC_CR_PLLI2SRDY) == 0) {
        if ((HAL_GetTick() - t0) > 50) return false;
    }
    return true;
}

/** 按给定引脚组合配置 GPIO（先关 I2S，避免切换时引脚冲突） */
static void mic_gpio_apply(const mic_pinset_t *p)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    SPI2->I2SCFGR = 0;

    GPIO_InitTypeDef g = {0};
    g.Mode  = GPIO_MODE_AF_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

    g.Pin = p->ws_pin; g.Alternate = p->ws_af; HAL_GPIO_Init(p->ws_port, &g);
    g.Pin = p->ck_pin; g.Alternate = p->ck_af; HAL_GPIO_Init(p->ck_port, &g);
    g.Pin = p->sd_pin; g.Alternate = p->sd_af; HAL_GPIO_Init(p->sd_port, &g);
}

/** 配置 SPI2 的 I2S 模式（**主机接收** / Philips / 24bit 数据 / 32bit 时隙）
 *
 * ⚠️ I2SCFG 是 bit9:8 两位（RM0383）：
 *      00 = 从机发送   01 = 从机接收   10 = 主机发送   11 = 主机接收
 *    早期版本只置 I2SCFG_0，等于配成"从机接收"——MCU 不产生 SCK/WS，
 *    而 INMP441 必须由主机供时钟，于是永远读不到数据（换线也没用）。
 */
static void mic_i2s_apply(void)
{
    __HAL_RCC_SPI2_CLK_ENABLE();

    SPI2->I2SPR = (MIC_I2SDIV & SPI_I2SPR_I2SDIV) |
                  ((MIC_I2SODD << 8) & SPI_I2SPR_ODD);
    SPI2->I2SCFGR = SPI_I2SCFGR_I2SMOD
                  | SPI_I2SCFGR_I2SCFG_0 | SPI_I2SCFGR_I2SCFG_1  /* 11 = 主机接收 */
                  | SPI_I2SCFGR_DATLEN_1   /* 10 = 32bit 数据：DR 一次读回整个槽，方便看位布局 */
                  | SPI_I2SCFGR_CHLEN;
    SPI2->I2SCFGR |= SPI_I2SCFGR_I2SE;
}

/** 直接读 32 位数据字；返回实际读到的字数（小于请求数 = 超时） */
static uint32_t mic_read_words(uint32_t *buf, uint32_t words, uint32_t timeout_ms)
{
    (void)SPI2->DR;                    /* 清溢出标志 */
    (void)SPI2->SR;

    uint32_t t0 = HAL_GetTick();
    uint32_t got = 0;
    for (uint32_t i = 0; i < words; i++) {
        while ((SPI2->SR & SPI_SR_RXNE) == 0) {
            if ((HAL_GetTick() - t0) > timeout_ms) return got;
        }
        buf[i] = SPI2->DR;
        got++;
    }
    return got;
}

/** 探测诊断量：每次试一套引脚就刷新，失败时打印出来定位问题
 *  got：实际读到的字数（< 请求数 = CK 没波形，RXNE 一直不置位）
 *  chg：与基准字不同的字数（= 0 说明 SD 线上是死电平）
 */
static uint32_t s_dbg_got = 0;
static uint32_t s_dbg_chg = 0;
static uint32_t s_dbg_ref = 0;

/** 试一套引脚：能读到数据、且数据有变化才算通过 */
static bool mic_probe(const mic_pinset_t *p)
{
    mic_gpio_apply(p);
    mic_i2s_apply();
    HAL_Delay(2);                      /* 让 CK/WS 先跑几个周期 */

    uint32_t got = mic_read_words(s_buf, MIC_PROBE_WORDS, MIC_PROBE_TIMEOUT_MS);
    s_dbg_got = got;
    s_dbg_chg = 0;
    s_dbg_ref = 0;
    if (got < MIC_PROBE_WORDS) return false;

    /* 基准取第 2 个字：第 1 个可能是清 OVR 后 DR 里的残留值，
     * 用它当基准会造成"假通过"（曾因此误判 SD 已经通了）。 */
    uint32_t ref = s_buf[1];
    s_dbg_ref = ref;
    uint32_t changed = 0;
    for (uint32_t i = 2; i < MIC_PROBE_WORDS; i++) {
        if (s_buf[i] != ref) changed++;
    }
    s_dbg_chg = changed;
    return changed > 8;                /* 完全不变 = 假数据 */
}

/* ───────────────── 对外接口 ───────────────── */

bool mic_init(void)
{
    if (s_ready) return true;

    if (!mic_pll2_config()) {
        mic_print("[mic] PLLI2S 起振超时（检查 25MHz 晶振）\r\n");
        return false;
    }

    /* 依次试所有候选组合，第一个能收到数据的即为生效配置。
     * 每套都打印 got/chg，失败时一眼能看出是"没时钟"还是"数据不动"：
     *   got < 256     → CK 没波形（RXNE 一直不置位）
     *   got=256 chg=0 → 有时钟但 SD 线上是死电平（悬空/未供电/WS 没接）
     */
    char line[120];
    for (uint32_t i = 0; i < MIC_SET_COUNT; i++) {
        bool ok = mic_probe(&SET_LIST[i]);
        snprintf(line, sizeof(line), "[mic] p%lu %s got=%lu/%u chg=%lu ref=%08lX\r\n",
                 (unsigned long)(i + 1), SET_LIST[i].name,
                 (unsigned long)s_dbg_got, (unsigned)MIC_PROBE_WORDS,
                 (unsigned long)s_dbg_chg, (unsigned long)s_dbg_ref);
        mic_print(line);
        if (ok) {
            s_active = &SET_LIST[i];
            break;
        }
    }
    if (!s_active) {
        mic_print("[mic] 所有引脚组合都没数据：查 3V3/GND 供电、SD 接线、L/R 是否接地\r\n");
        return false;
    }

    s_ready = true;
    return true;
}

const char *mic_active_pinset(void)
{
    return s_active ? s_active->name : "(未配置)";
}

bool mic_read_raw(uint32_t *buf, uint16_t words, uint32_t timeout_ms)
{
    if (!s_ready || !buf || words == 0) return false;
    return mic_read_words(buf, words, timeout_ms) == words;
}

uint32_t mic_last_peak(void)
{
    return s_last_peak;
}

#if 0
/* ⚠️ 排查工具，默认不编译（否则 ARMCC 会警告"定义了没引用"）。
 * 需要时把下面的 #if 0 改成 #if 1，并在 mic_boot_selftest() 里取消
 * mic_pin_id_test(); 的注释即可。
 *
 * 用芯片自己当"通断表"，查出那根待测飞线焊在了哪个脚上。
 *
 * 用法（不需要万用表）：
 *   1) 把待查那根线的**模块端**拔下来，插到 2×9 上排第 2 孔（PB2，本测试当信号源）
 *   2) 复位，看串口 [mic] pinid 那一行
 *   3) 哪个脚打印 1，就说明这根线焊的是哪个脚；全 0 = 线断/虚焊/接触不良
 *
 * 原理：PB2 输出高电平 → 经"焊盘→母座→核心板引脚→飞线"回到探测脚，
 *       探测脚全设下拉输入，被拉高的那个就是同一条网络。
 */
static void mic_pin_id_test(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Speed = GPIO_SPEED_FREQ_LOW;

    /* 信号源：PB2 推挽输出高 */
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Pin  = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOB, &g);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2, GPIO_PIN_SET);

    /* 探测脚：全部下拉输入 */
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLDOWN;
    const uint16_t probe[5] = { GPIO_PIN_9, GPIO_PIN_12, GPIO_PIN_13, GPIO_PIN_14, GPIO_PIN_15 };
    for (int i = 0; i < 5; i++) {
        g.Pin = probe[i];
        HAL_GPIO_Init(GPIOB, &g);
    }
    HAL_Delay(5);

    char line[96];
    snprintf(line, sizeof(line),
             "[mic] pinid PB9=%d PB12=%d PB13=%d PB14=%d PB15=%d\r\n",
             HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9)  == GPIO_PIN_SET,
             HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_12) == GPIO_PIN_SET,
             HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_13) == GPIO_PIN_SET,
             HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_14) == GPIO_PIN_SET,
             HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_15) == GPIO_PIN_SET);
    mic_print(line);

    /* 收工：PB2 恢复成普通输入，探测脚也放掉 */
    g.Pin = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOB, &g);
}
#endif /* 引脚自测工具的 #if 0 */

void mic_boot_selftest(void)
{
#if MIC_SELFTEST_ON_BOOT
    /* 固件版本戳：先打印，再探测。任何时候都能一眼确认烧进去的是哪一版——
     * 之前有过改了代码没重编、F8 把旧固件烧进去，对着旧行为查了半天接线。 */
    mic_print("[mic] fw=v12 means\r\n");

    /* 引脚身份自测：把待查那根线的模块端插到 2×9 上排第 2 孔（PB2）后复位，
     * 串口会打印 pinid 那一行，指出这根线到底焊在哪个脚上。 */
    /* 引脚自测已确认飞线焊在 PB12 上，默认不再执行；需要时取消注释 */
    /* mic_pin_id_test(); */

    if (!mic_init()) return;

    char line[80];
    snprintf(line, sizeof(line), "[mic] 引脚组合：%s\r\n", mic_active_pinset());
    mic_print(line);
    mic_print("[mic] 自检开始，采 1 秒，请随便说句话\r\n");
    mic_print("[mic] lvl: ");

    uint32_t peak_l = 0, peak_r = 0;
    /* 把 1 秒切成 4 段，分别记峰值。
     * 用法：前半秒别出声、后半秒大声喊 —— 若 Q2/Q3 明显大于 Q0/Q1，
     * 说明音频链路是通的（只是取位不对）；若四段一样，说明没拿到真实音频。 */
    uint32_t q_peak[4] = {0, 0, 0, 0};
    /* 逐位翻转统计：flip_cnt[字序%4][bit] 记录该位翻转了多少次 */
    uint32_t flip_cnt[4][32];
    uint32_t flip_prev[4] = {0, 0, 0, 0};
    uint32_t q_flip[4] = {0, 0, 0, 0};   /* 每 1/4 秒的总翻转数 */
    /* 按三种"位偏移"分别累计幅度平均值，找出真正承载音频的那一种：
     *   m16 = word >> 16      （数据在时隙高位）
     *   m8  = (word >> 8) & 0xFFFF（数据整体偏移 8 位）
     *   m0  = word & 0xFFFF   （数据在低位）
     * 只累加绝对值，噪音（高阻位）在均值里影响小，音量变化能反映出来。 */
    uint64_t sum_m16[4] = {0, 0, 0, 0};
    uint64_t sum_m8[4]  = {0, 0, 0, 0};
    uint64_t sum_m0[4]  = {0, 0, 0, 0};
    uint32_t nq[4] = {0, 0, 0, 0};
    int      flip_ready = 0;
    for (int a = 0; a < 4; a++) {
        for (int b = 0; b < 32; b++) flip_cnt[a][b] = 0;
    }
    uint32_t pk_hi = 0;               /* 高 16 位峰值 */
    uint32_t pk_lo = 0;               /* 低 16 位峰值 */
    uint64_t sum_l2 = 0, sum_r2 = 0;
    uint32_t n = 0, printed = 0;

    for (uint32_t c = 0; c < MIC_CHUNKS; c++) {
        if (!mic_read_raw(s_buf, MIC_CHUNK_WORDS, 300)) {
            mic_print("\r\n[mic] 读取超时（采集中断）\r\n");
            return;
        }

        if (c == 0) {
            /* 打印前 4 帧的比特图（每帧 2 个 32 位字），用于观察位布局 */
            for (int f = 0; f < 2; f++) {
                for (int h = 0; h < 2; h++) {
                    uint32_t w = s_buf[f * 2 + h];
                    char bits[33];
                    for (int b = 0; b < 32; b++) {
                        bits[b] = (w & (0x80000000u >> b)) ? '1' : '0';
                    }
                    bits[32] = '\0';
                    snprintf(line, sizeof(line), "[mic] f%d.%d %s\r\n", f, h, bits);
                    mic_print(line);
                }
            }
        }

        /* 逐位统计"翻转次数"：某一 bit 在相邻采样之间翻转得多，
         * 就说明它在跟着声音变化。安静时音频位几乎不翻，喊叫时会大量翻转。
         * 同时按 1/4 秒累计总翻转数，作为"音量"指标——
         * 它不会被数据里恒定的数值（如 0x8000）撑满，比峰值可靠。 */
        uint32_t qi_f = (c * 4u) / MIC_CHUNKS;
        if (qi_f > 3) qi_f = 3;
        for (uint32_t i = 0; i < MIC_CHUNK_WORDS; i++) {
            uint32_t w = s_buf[i];

            /* 三种位偏移的幅度累计 */
            {
                int32_t a16 = (int32_t)((int16_t)((w >> 16) & 0xFFFFu));
                int32_t a8  = (int32_t)((int16_t)((w >> 8)  & 0xFFFFu));
                int32_t a0  = (int32_t)((int16_t)(w & 0xFFFFu));
                sum_m16[qi_f] += (uint64_t)(a16 < 0 ? -a16 : a16);
                sum_m8[qi_f]  += (uint64_t)(a8  < 0 ? -a8  : a8);
                sum_m0[qi_f]  += (uint64_t)(a0  < 0 ? -a0  : a0);
                nq[qi_f]++;
            }

            uint32_t pos = i & 3u;
            if (flip_ready) {
                uint32_t x = w ^ flip_prev[pos];
                if (x) {
                    q_flip[qi_f] += 1;
                    for (int b = 0; b < 32; b++) {
                        if (x & (0x80000000u >> b)) flip_cnt[pos][b]++;
                    }
                }
            }
            flip_prev[pos] = w;
        }
        flip_ready = 1;

        uint32_t blk_peak = 0;
        for (uint32_t i = 0; i + 1 < MIC_CHUNK_WORDS; i += 2) {
            uint32_t wl = s_buf[i], wr = s_buf[i + 1];

            int32_t l = (int32_t)((int16_t)(wl >> 16));
            int32_t r = (int32_t)((int16_t)(wr >> 16));
            uint32_t al = (uint32_t)(l < 0 ? -l : l);
            uint32_t ar = (uint32_t)(r < 0 ? -r : r);

            if (al > peak_l) peak_l = al;
            if (ar > peak_r) peak_r = ar;
            if (al > blk_peak) blk_peak = al;
            sum_l2 += (uint64_t)(l * l);
            sum_r2 += (uint64_t)(r * r);
            n++;

            /* 高/低 16 位各算峰值；同时记录本块最大值到所在的那 1/4 秒 */
            {
                int32_t b0 = (int32_t)((int16_t)(wl & 0xFFFFu));
                int32_t b1 = (int32_t)((int16_t)(wr & 0xFFFFu));
                uint32_t a0 = (uint32_t)(b0 < 0 ? -b0 : b0);
                uint32_t a1 = (uint32_t)(b1 < 0 ? -b1 : b1);
                if (a0 > pk_lo) pk_lo = a0;
                if (a1 > pk_lo) pk_lo = a1;

                int32_t h0 = (int32_t)((int16_t)(wl >> 16));
                int32_t h1 = (int32_t)((int16_t)(wr >> 16));
                uint32_t q0 = (uint32_t)(h0 < 0 ? -h0 : h0);
                uint32_t q1 = (uint32_t)(h1 < 0 ? -h1 : h1);
                if (q0 > pk_hi) pk_hi = q0;
                if (q1 > pk_hi) pk_hi = q1;

                uint32_t m = a0 > a1 ? a0 : a1;
                if (q0 > m) m = q0;
                if (q1 > m) m = q1;
                uint32_t qi = (c * 4u) / MIC_CHUNKS;   /* 落在第几个 1/4 秒 */
                if (qi > 3) qi = 3;
                if (m > q_peak[qi]) q_peak[qi] = m;
            }
        }

        char ch = '0';
        if (blk_peak > 8000)      ch = '9';
        else if (blk_peak > 4000) ch = '7';
        else if (blk_peak > 2000) ch = '5';
        else if (blk_peak > 800)  ch = '3';
        else if (blk_peak > 200)  ch = '1';
        line[0] = ch; line[1] = '\0';
        mic_print(line);

        if (++printed % 32 == 0) mic_print("\r\n[mic] lvl: ");
    }
    mic_print("\r\n");

    uint32_t rms_l = n ? isqrt_u64(sum_l2 / n) : 0;
    uint32_t rms_r = n ? isqrt_u64(sum_r2 / n) : 0;
    s_last_peak = peak_l;

    snprintf(line, sizeof(line), "[mic] L: peak=%lu rms=%lu\r\n",
             (unsigned long)peak_l, (unsigned long)rms_l);
    mic_print(line);
    snprintf(line, sizeof(line), "[mic] R: peak=%lu rms=%lu  frames=%lu\r\n",
             (unsigned long)peak_r, (unsigned long)rms_r, (unsigned long)n);
    mic_print(line);

    /* 高/低 16 位峰值 + 4 段峰值（判断"声音有没有进来"的关键） */
    snprintf(line, sizeof(line), "[mic] pk hi=%lu lo=%lu\r\n",
             (unsigned long)pk_hi, (unsigned long)pk_lo);
    mic_print(line);
    snprintf(line, sizeof(line), "[mic] Q0=%lu Q1=%lu Q2=%lu Q3=%lu\r\n",
             (unsigned long)q_peak[0], (unsigned long)q_peak[1],
             (unsigned long)q_peak[2], (unsigned long)q_peak[3]);
    mic_print(line);

    /* 逐位活动图：flip_cnt 归一成 0~9，哪个 bit 位是 9 就说明它在跟着声音翻 */
    uint32_t fmax = 1;
    for (int a = 0; a < 4; a++)
        for (int b = 0; b < 32; b++)
            if (flip_cnt[a][b] > fmax) fmax = flip_cnt[a][b];
    for (int a = 0; a < 4; a++) {
        char bar[33];
        for (int b = 0; b < 32; b++) {
            uint32_t d = (uint32_t)(((uint64_t)flip_cnt[a][b] * 9u) / fmax);
            bar[b] = (char)('0' + (d > 9 ? 9 : d));
        }
        bar[32] = '\0';
        snprintf(line, sizeof(line), "[mic] act%d %s\r\n", a, bar);
        mic_print(line);
    }

    /* 三种位偏移的"每 1/4 秒平均幅度"——哪一组跟着喊叫变大，就用哪一组取样本 */
    {
        uint64_t m16[4], m8[4], m0[4];
        for (int q = 0; q < 4; q++) {
            uint32_t d = nq[q] ? nq[q] : 1;
            m16[q] = sum_m16[q] / d;
            m8[q]  = sum_m8[q]  / d;
            m0[q]  = sum_m0[q]  / d;
        }
        snprintf(line, sizeof(line), "[mic] m16 %lu %lu %lu %lu\r\n",
                 (unsigned long)m16[0], (unsigned long)m16[1],
                 (unsigned long)m16[2], (unsigned long)m16[3]);
        mic_print(line);
        snprintf(line, sizeof(line), "[mic] m8  %lu %lu %lu %lu\r\n",
                 (unsigned long)m8[0], (unsigned long)m8[1],
                 (unsigned long)m8[2], (unsigned long)m8[3]);
        mic_print(line);
        snprintf(line, sizeof(line), "[mic] m0  %lu %lu %lu %lu\r\n",
                 (unsigned long)m0[0], (unsigned long)m0[1],
                 (unsigned long)m0[2], (unsigned long)m0[3]);
        mic_print(line);
    }

    /* 音量指标：每 1/4 秒的总翻转次数。前半秒安静、后半秒喊，后两段应明显更大 */
    snprintf(line, sizeof(line), "[mic] flipQ %lu %lu %lu %lu\r\n",
             (unsigned long)q_flip[0], (unsigned long)q_flip[1],
             (unsigned long)q_flip[2], (unsigned long)q_flip[3]);
    mic_print(line);

    uint32_t qmax = q_peak[0];
    uint32_t qmin = q_peak[0];
    for (int i = 1; i < 4; i++) if (q_peak[i] > qmax) qmax = q_peak[i];
    for (int i = 1; i < 4; i++) if (q_peak[i] < qmin) qmin = q_peak[i];

    if (qmax > 200 && qmax > qmin * 2) {
        mic_print("[mic] OK：声音进来了（后段明显大于前段）\r\n");
    } else if (qmax > 200) {
        mic_print("[mic] 有信号但四段接近：不随声音变化，查 WS（帧同步）\r\n");
    } else {
        mic_print("[mic] 四段都接近 0：SD 线或供电问题\r\n");
    }
#endif /* MIC_SELFTEST_ON_BOOT */
}
