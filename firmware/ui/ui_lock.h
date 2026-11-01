/**
 * @file    ui_lock.h
 * @brief   锁屏「层」：大时钟 + 日期（不自己建屏幕，由 ui_stage 嵌入）
 *
 * 手势不在这里处理——拖拽判定统一交给 ui_stage，这样锁屏层只管画。
 */
#ifndef UI_LOCK_H
#define UI_LOCK_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 把锁屏内容画进 parent（parent 是 240×280 的容器）*/
void ui_lock_build(lv_obj_t *parent);

/** 更新锁屏上的时间与日期（RTC 定时器调用） */
void ui_lock_set_time(const char *time_str, const char *date_str);

#ifdef __cplusplus
}
#endif

#endif /* UI_LOCK_H */
