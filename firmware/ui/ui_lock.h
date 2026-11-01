/**
 * @file    ui_lock.h
 * @brief   锁屏页：大时钟 + 日期，上滑解锁（无任何文字提示）
 */
#ifndef UI_LOCK_H
#define UI_LOCK_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 创建锁屏页（永久屏，只创建一次） */
lv_obj_t *ui_lock_create(void);

/** 切到锁屏页（休眠唤醒 / 开机时用） */
void ui_lock_show(void);

/** 更新锁屏上的时间与日期（RTC 定时器调用） */
void ui_lock_set_time(const char *time_str, const char *date_str);

#ifdef __cplusplus
}
#endif

#endif /* UI_LOCK_H */
