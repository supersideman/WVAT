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
 * I2S2 的两套可用引脚（F411 数据手册的复用表里都有）：
 *   主用（AF5）：WS=PB12  CK=PB13  SD=PB15   ← 核心板右排最上面两行
 *   备用（AF6）：WS=PB9   CK=PB10  SD=PC3    ← CK/SD 恰好在扩展板 2×9 上
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

/* 候选组合，开机依次探测，谁有数据用谁（顺序 = 优先级） */
static const mic_pinset_t SET_LIST[] = {
    /* ① 全主用（三根都飞线到 PB12/13/15 时命中）*/
    { GPIOB, GPIO_PIN_12, GPIO_AF5_SPI2,
      GPIOB, GPIO_PIN_13, GPIO_AF5_SPI2,
      GPIOB, GPIO_PIN_15, GPIO_AF5_SPI2, "PB12/PB13/PB15 全 AF5" },

    /* ② 混合：WS 飞 PB12，CK/SD 走扩展板 2×9 的 PB10/PC3（最省焊的方案）*/
    { GPIOB, GPIO_PIN_12, GPIO_AF5_SPI2,
      GPIOB, GPIO_PIN_10, GPIO_AF6_SPI2,
      GPIOC, GPIO_PIN_3,  GPIO_AF6_SPI2, "PB12 + PB10 + PC3 混合" },

    /* ③ 混合的另一种 AF 猜测：PB12 若按 AF6 才通，这里兜住 */
    { GPIOB, GPIO_PIN_12, GPIO_AF6_SPI2,
      GPIOB, GPIO_PIN_10, GPIO_AF6_SPI2,
      GPIOC, GPIO_PIN_3,  GPIO_AF6_SPI2, "PB12(AF6) + PB10 + PC3 混合" },

    /* ④ 全备用：WS 飞 PB9，CK/SD 走 2×9 */
    { GPIOB, GPIO_PIN_9,  GPIO_AF6_SPI2,
      GPIOB, GPIO_PIN_10, GPIO_AF6_SPI2,
      GPIOC, GPIO_PIN_3,  GPIO_AF6_SPI2, "PB9 + PB10 + PC3 全 AF6" },
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

/** 配置 SPI2 的 I2S 模式（主机接收 / Philips / 24bit 数据 / 32bit 时隙） */
static void mic_i2s_apply(void)
{
    __HAL_RCC_SPI2_CLK_ENABLE();

    SPI2->I2SPR = (MIC_I2SDIV & SPI_I2SPR_I2SDIV) |
                  ((MIC_I2SODD << 8) & SPI_I2SPR_ODD);
    SPI2->I2SCFGR = SPI_I2SCFGR_I2SMOD
                  | SPI_I2SCFGR_I2SCFG_0
                  | SPI_I2SCFGR_DATLEN_0
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

/** 试一套引脚：能读到数据、且数据有变化才算通过 */
static bool mic_probe(const mic_pinset_t *p)
{
    mic_gpio_apply(p);
    mic_i2s_apply();
    HAL_Delay(2);                      /* 让 CK/WS 先跑几个周期 */

    uint32_t got = mic_read_words(s_buf, MIC_PROBE_WORDS, MIC_PROBE_TIMEOUT_MS);
    if (got < MIC_PROBE_WORDS) return false;

    uint32_t changed = 0;
    for (uint32_t i = 1; i < MIC_PROBE_WORDS; i++) {
        if (s_buf[i] != s_buf[0]) changed++;
    }
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

    /* 依次试所有候选组合，第一个能收到数据的即为生效配置 */
    for (uint32_t i = 0; i < MIC_SET_COUNT; i++) {
        if (mic_probe(&SET_LIST[i])) {
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

void mic_boot_selftest(void)
{
#if MIC_SELFTEST_ON_BOOT
    if (!mic_init()) return;

    char line[80];
    snprintf(line, sizeof(line), "[mic] 引脚组合：%s\r\n", mic_active_pinset());
    mic_print(line);
    mic_print("[mic] 自检开始，采 1 秒，请随便说句话\r\n");
    mic_print("[mic] lvl: ");

    uint32_t peak_l = 0, peak_r = 0;
    uint64_t sum_l2 = 0, sum_r2 = 0;
    uint32_t n = 0, printed = 0;

    for (uint32_t c = 0; c < MIC_CHUNKS; c++) {
        if (!mic_read_raw(s_buf, MIC_CHUNK_WORDS, 300)) {
            mic_print("\r\n[mic] 读取超时（采集中断）\r\n");
            return;
        }

        uint32_t blk_peak = 0;
        for (uint32_t i = 0; i + 1 < MIC_CHUNK_WORDS; i += 2) {
            int32_t l = (int32_t)((int16_t)(s_buf[i] >> 16));
            int32_t r = (int32_t)((int16_t)(s_buf[i + 1] >> 16));
            uint32_t al = (uint32_t)(l < 0 ? -l : l);
            uint32_t ar = (uint32_t)(r < 0 ? -r : r);

            if (al > peak_l) peak_l = al;
            if (ar > peak_r) peak_r = ar;
            if (al > blk_peak) blk_peak = al;
            sum_l2 += (uint64_t)(l * l);
            sum_r2 += (uint64_t)(r * r);
            n++;
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

    if (peak_l > 200) {
        mic_print("[mic] OK：左声道有信号，接线正确\r\n");
    } else if (peak_r > 200) {
        mic_print("[mic] 右声道有声、左声道无：L/R 接到了 3V3（应接 GND）\r\n");
    } else {
        mic_print("[mic] 两声道都接近 0：查 SD 线、3V3/GND 供电\r\n");
    }
#endif /* MIC_SELFTEST_ON_BOOT */
}
