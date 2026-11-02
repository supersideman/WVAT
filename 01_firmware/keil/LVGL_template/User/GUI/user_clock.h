/**
 * @file    user_clock.h
 * @brief   真实时钟：读 STM32 内部 RTC（LSE 32.768kHz），并定时刷新界面
 *
 * 背景：作者模板里**没有配置 RTC**（.ioc 把 PC14/PC15 设成了 LSE 外部晶振，
 *       但 SystemClock_Config 只开了 HSE，RTC 从未初始化）。
 *       这里手工把 LSE → RTC 时钟源 → RTC 外设 这条链补全。
 *
 * 时间来源：开机时 RTC 里若没有有效时间，界面显示 "--:--"；
 *           PC 网关会周期性下发 OV+EV=TIME,HH:MM:SS,YYYY-MM-DD 自动对时。
 */
#ifndef USER_CLOCK_H
#define USER_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 RTC（开 LSE、选 RTC 时钟源、初始化外设）。失败也不影响其它功能 */
void clock_init(void);

/** 建立 1 秒刷新定时器（必须在 LVGL 起来之后调用） */
void clock_start_ui_timer(void);

/** RTC 里是否已有有效时间（备份寄存器标记） */
bool clock_is_set(void);

/** 设置时间与日期（由 PC 授时调用 / 或手动调用） */
void clock_set(uint16_t year, uint8_t month, uint8_t day,
               uint8_t hour, uint8_t minute, uint8_t second);

/** 当前时间的字符串形式："HH:MM"（未设置时返回 "--:--"） */
const char *clock_time_str(void);

/** 当前日期的字符串形式："MM/DD"（未设置时返回 "--/--"） */
const char *clock_date_str(void);

#ifdef __cplusplus
}
#endif

#endif /* USER_CLOCK_H */
