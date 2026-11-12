/**
 * @file    user_i2s.c
 * @brief   I2S2 数字麦克风采集 —— 寄存器级实现（不依赖 HAL I2S）
 *
 * 为什么不用 HAL 的 I2S：
 *   ① 模板工程里根本没有 stm32f4xx_hal_i2s.c（和当初 RTC 一样缺文件）；
 *   ② **HAL 的轮询接收有 bug**：源码里是
 *          (*pRxBuffPtr) = (uint16_t)hi2s->Instance->DR;
 *      INMP441 的 24bit 数据在 32bit 时隙里是**高位对齐**的，
 *      取低 16 位等于只留下最低 8 位——数据全废。
 *      所以这里直接读 32 位的 DR，绕过 HAL。
 *
 * 硬件连接（焊在核心板背面丝印对应的焊点）：
 *   INMP441 SCK → PB13 (I2S2_CK)
 *   INMP441 WS  → PB12 (I2S2_WS)
 *   INMP441 SD  → PB15 (I2S2_SD)
 *   INMP441 VDD → 3V3      （⚠️ 绝不能接 5V）
 *   INMP441 GND → GND
 *   INMP441 L/R → GND      （选左声道）
 *
 * 时钟（本板 25MHz HSE、PLLM=12 → PLL 输入 2.0833MHz）：
 *   PLLI2S N=58, R=2 → VCO 120.83MHz → I2S 时钟 60.417MHz
 *   16kHz × 64bit/帧 = 1.024MHz → I2SDIV=29 + ODD=1 → 1.0240MHz
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
#define MIC_I2SDIV   29u   /* 2*29+1 = 59 分频 → 60.417/59 = 1.024 MHz */
#define MIC_I2SODD   1u

/* 自检：一次读 512 个 32bit 字 = 256 帧 = 16ms @16kHz；63 块 ≈ 1 秒 */
#define MIC_CHUNK_WORDS 512u
#define MIC_CHUNKS      63u

static bool     s_ready = false;
static uint32_t s_last_peak = 0;
static uint32_t s_buf[MIC_CHUNK_WORDS];

/* ───────────────── 打印工具 ───────────────── */

static void mic_print(const char *s)
{
    uint16_t n = (uint16_t)strlen(s);
    if (n) HAL_UART_Transmit(&huart1, (uint8_t *)s, n, 100);
}

/* ───────────────── 整数平方根（避免依赖 libm） ───────────────── */

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

/* ───────────────── 底层初始化（寄存器级） ───────────────── */

/** 配 PLLI2S：I2S 的时钟源，不开它 SPI2 拿不到时钟 */
static bool mic_pll2_config(void)
{
    /* 配之前先关掉 PLLI2S */
    RCC->CR &= ~RCC_CR_PLLI2SON;
    while (RCC->CR & RCC_CR_PLLI2SON) { }

    /* PLLI2SN 在 bit14:6，PLLI2SR 在 bit30:28（F411 无 PLLI2SM，
       PLLI2S 的输入就是主 PLL 的输入 HSE/PLLM = 2.0833MHz） */
    uint32_t v = 0;
    v |= (MIC_PLLI2S_N & 0x1FFu) << 6;
    v |= (MIC_PLLI2S_R & 0x7u) << 28;
    RCC->PLLI2SCFGR = v;

    RCC->CR |= RCC_CR_PLLI2SON;
    uint32_t t0 = HAL_GetTick();
    while ((RCC->CR & RCC_CR_PLLI2SRDY) == 0) {
        if (HAL_GetTick() - t0 > 50) return false;   /* 超时 = 晶振或参数有问题 */
    }
    return true;
}

/** 配 GPIO：PB12/13/15 全部 AF5（I2S2） */
static void mic_gpio_config(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Pin       = GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_15;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF5_SPI2;            /* I2S2 = SPI2 的 AF5 */
    HAL_GPIO_Init(GPIOB, &g);
}

/** 配 SPI2 为 I2S 主机接收 */
static void mic_i2s_config(void)
{
    __HAL_RCC_SPI2_CLK_ENABLE();

    /* 先关 I2S，才能改配置 */
    SPI2->I2SCFGR = 0;

    /* I2SPR：I2SDIV + ODD，MCLK 输出关掉（INMP441 不需要 MCLK） */
    SPI2->I2SPR = (MIC_I2SDIV & SPI_I2SPR_I2SDIV) |
                  ((MIC_I2SODD << 8) & SPI_I2SPR_ODD);

    /* I2SCFGR：
         I2SMOD=1        I2S 模式
         I2SCFG=01       主机接收（Master RX）
         I2SSTD=00       Philips 标准（INMP441 就是 Philips）
         CKPOL=0          时钟空闲低电平（数据在上升沿采样）
         DATLEN=01       24bit 数据
         CHLEN=1         32bit 时隙（INMP441 每声道占 32bit） */
    uint32_t cfgr = SPI_I2SCFGR_I2SMOD
                  | SPI_I2SCFGR_I2SCFG_0
                  | SPI_I2SCFGR_DATLEN_0
                  | SPI_I2SCFGR_CHLEN;
    SPI2->I2SCFGR = cfgr;

    /* 使能 I2S：主机模式下会立刻输出 CK/WS，开始采样 */
    SPI2->I2SCFGR |= SPI_I2SCFGR_I2SE;
}

bool mic_init(void)
{
    if (s_ready) return true;

    mic_gpio_config();

    if (!mic_pll2_config()) {
        mic_print("[mic] PLLI2S 起振超时（检查 25MHz 晶振或分频参数）\r\n");
        return false;
    }
    mic_i2s_config();

    s_ready = true;
    return true;
}

/**
 * 直接读 32 位数据字（左/右声道交替）。
 *
 * 为什么是 32 位：24bit 样本在 32bit 时隙里高位对齐，
 * 必须整字读取，取高 16 位才等于 16bit 音频样本。
 */
static bool mic_read_words(uint32_t *buf, uint32_t words, uint32_t timeout_ms)
{
    if (!s_ready || !buf || words == 0) return false;

    /* 清溢出标志：读 DR 再读 SR */
    (void)SPI2->DR;
    (void)SPI2->SR;

    uint32_t t0 = HAL_GetTick();
    for (uint32_t i = 0; i < words; i++) {
        while ((SPI2->SR & SPI_SR_RXNE) == 0) {
            if ((HAL_GetTick() - t0) > timeout_ms) return false;
        }
        buf[i] = SPI2->DR;             /* 读 DR 同时清 RXNE */
    }
    return true;
}

/* ───────────────── 对外接口 ───────────────── */

bool mic_read_raw(uint32_t *buf, uint16_t words, uint32_t timeout_ms)
{
    return mic_read_words(buf, words, timeout_ms);
}

uint32_t mic_last_peak(void)
{
    return s_last_peak;
}

/**
 * 开机自检：采 1 秒，统计左右声道峰值/RMS，打印简易电平条。
 *
 * 判读：
 *   数值随环境声跳动、说话时明显变大 → 接线正确
 *   peak 恒为 0                      → SD 没通 / 麦克风没供电
 *   peak 接近满量程乱跳               → 时钟或 L/R 接错，或 SCK/WS 反了
 *   读取超时                         → I2S 完全没有数据（CK 没出来/接线全错）
 */
void mic_boot_selftest(void)
{
#if MIC_SELFTEST_ON_BOOT
    if (!mic_init()) return;

    mic_print("[mic] 自检开始，采 1 秒，请随便说句话\r\n");
    mic_print("[mic] lvl: ");

    uint32_t peak_l = 0, peak_r = 0;
    uint64_t sum_l2 = 0, sum_r2 = 0;
    uint32_t n = 0;
    char line[72];
    uint32_t printed = 0;

    for (uint32_t c = 0; c < MIC_CHUNKS; c++) {
        if (!mic_read_words(s_buf, MIC_CHUNK_WORDS, 300)) {
            mic_print("\r\n[mic] 读取超时：I2S 没有数据。查 CK/WS/SD 三根线、3V3、GND\r\n");
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

        /* 每块一个字符的电平等级，方便肉眼看出有没有声音 */
        char ch = '0';
        if (blk_peak > 8000)      ch = '9';
        else if (blk_peak > 4000) ch = '7';
        else if (blk_peak > 2000) ch = '5';
        else if (blk_peak > 800)  ch = '3';
        else if (blk_peak > 200)  ch = '1';
        line[0] = ch;
        line[1] = '\0';
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
        mic_print("[mic] OK：左声道有信号\r\n");
    } else if (peak_r > 200 && peak_l <= 200) {
        mic_print("[mic] 右声道有信号、左声道无：L/R 接成了 3V3（应接 GND）\r\n");
    } else {
        mic_print("[mic] 两声道都接近 0：查 3V3/GND 供电、SD 是否焊在 PB15\r\n");
    }
#endif /* MIC_SELFTEST_ON_BOOT */
}
