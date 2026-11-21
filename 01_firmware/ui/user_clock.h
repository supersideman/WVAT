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

/** 当前"日"的两位字符串："21"（未设置时返回 "--"）——日期页的大字 */
const char *clock_day_str(void);

/** 当前"年/月"的字符串："2026/11"（未设置时返回 "----/--"） */
const char *clock_year_month_str(void);

/** 星期的英文三字母缩写："MON".."SUN"（未设置时返回 "---"）
 *  ⚠️ 由年月日算出来（Sakamoto 算法），**不读 RTC 的 weekday 字段**——
 *     那个字段我们的授时流程从来没写过，读出来是 0，不可信。 */
const char *clock_weekday_str(void);

/** 立刻刷新缓存并推给所有页面（主界面/锁屏/日期页）。
 *  给"页面刚显示出来、想马上是最新值"的场合用（比如 ui_date_open）。 */
void clock_publish(void);

#ifdef __cplusplus
}
#endif

#endif /* USER_CLOCK_H */
