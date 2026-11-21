/**
 * @file    user_backlight.c
 * @brief   屏幕背光调光：PA15 = TIM2_CH1 PWM，1 kHz
 *
 * 背景
 * ----
 * BSP/LCD/lcd_init.c 把背光（BLK）当成**纯 GPIO 开关**用：
 *     BLK_PORT = GPIOA, BLK_PIN = GPIO_PIN_15
 * 所以原本只有"全亮 / 全灭"两档，做不了亮度调节。
 *
 * 为什么选 TIM2_CH1
 * -----------------
 * - PA15 的 TIM2_CH1 复用功能是 AF1，板子上 TIM2 完全空闲
 *   （只有 TIM11 被 HAL 时基占用）；
 * - 不需要额外飞线、不占新的引脚。
 *
 * 频率选择
 * --------
 * - TIM2 计数时钟 = 100 MHz（APB1 = 50 MHz，定时器时钟翻倍）
 * - PSC = 99    → 计数 1 MHz
 * - ARR = 999   → PWM 频率 1 kHz，占空比分辨率 0.1%
 * 1 kHz 远高于人眼闪烁感知，背光不会看到频闪。
 *
 * ⚠️ 注意
 * -------
 * 本模块会在 lcd_init() **之后**把 PA15 从 GPIO 输出改成 AF 复用，
 * 所以必须在 LCD 初始化完成后再调用 bl_init()（现在放在 ui_stage_init 里）。
 * 若 PWM 起不来，会自动退化成"GPIO 开关"（>0 就点亮），不会黑屏。
 */
#include "user_backlight.h"

#include <stdio.h>

#include "main.h"      /* STM32 HAL 与 CubeMX 宏 */

#define BL_TIM          TIM2
#define BL_CHANNEL      TIM_CHANNEL_1
#define BL_GPIO_PORT    GPIOA
#define BL_GPIO_PIN     GPIO_PIN_15

#define BL_PSC          99u      /* 100 MHz / 100 = 1 MHz */
#define BL_ARR          999u     /* 1 MHz / 1000 = 1 kHz */

static TIM_HandleTypeDef s_htim;
static uint8_t  s_pct = 100;
static bool     s_pwm_ok = false;

void bl_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_TIM2_CLK_ENABLE();

    /* PA15 → TIM2_CH1（AF1）*/
    GPIO_InitTypeDef g = {0};
    g.Pin       = BL_GPIO_PIN;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_LOW;
    g.Alternate = GPIO_AF1_TIM2;
    HAL_GPIO_Init(BL_GPIO_PORT, &g);

    s_htim.Instance               = BL_TIM;
    s_htim.Init.Prescaler         = BL_PSC;
    s_htim.Init.CounterMode       = TIM_COUNTERMODE_UP;
    s_htim.Init.Period            = BL_ARR;
    s_htim.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    s_htim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(&s_htim) != HAL_OK) {
        s_pwm_ok = false;
        bl_set_percent(100);          /* 退化：直接点亮 */
        printf("[bl] PWM 初始化失败，退化为开关\r\n");
        return;
    }

    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode     = TIM_OCMODE_PWM1;
    oc.Pulse      = BL_ARR + 1;       /* 100% */
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&s_htim, &oc, BL_CHANNEL) != HAL_OK) {
        s_pwm_ok = false;
        bl_set_percent(100);
        printf("[bl] PWM 通道配置失败，退化为开关\r\n");
        return;
    }

    HAL_TIM_PWM_Start(&s_htim, BL_CHANNEL);
    s_pwm_ok = true;
    bl_set_percent(100);
    printf("[bl] TIM2_CH1 PWM ok（1kHz, PA15 背光）\r\n");
}

void bl_set_percent(uint8_t pct)
{
    if (pct > 100) pct = 100;
    s_pct = pct;

    if (!s_pwm_ok) {
        /* PWM 没起来：当开关用，非零就点亮 */
        HAL_GPIO_WritePin(BL_GPIO_PORT, BL_GPIO_PIN,
                          pct ? GPIO_PIN_SET : GPIO_PIN_RESET);
        return;
    }
    uint32_t pulse = ((uint32_t)pct * (BL_ARR + 1u)) / 100u;
    __HAL_TIM_SET_COMPARE(&s_htim, BL_CHANNEL, pulse);
}

uint8_t bl_get_percent(void)
{
    return s_pct;
}
