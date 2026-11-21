/**
 * @file    user_backlight.h
 * @brief   屏幕背光调光（把 PA15 从"纯 GPIO 开关"升级成 PWM）
 */
#ifndef USER_BACKLIGHT_H
#define USER_BACKLIGHT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化背光 PWM。
 * 在 ui_stage_init() 里调用（此时 LCD 已初始化完毕，
 * 会把 lcd_init.c 设成 GPIO 输出的 PA15 改成 TIM2_CH1 复用）。
 */
void bl_init(void);

/** 设置亮度百分比 0~100 */
void bl_set_percent(uint8_t pct);

/** 读取当前亮度百分比 */
uint8_t bl_get_percent(void);

#ifdef __cplusplus
}
#endif

#endif /* USER_BACKLIGHT_H */
