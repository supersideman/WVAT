/**
 * @file    ui_main.h
 * @brief   主界面（B 版）：时钟 + 日期 + 最近结果 + 4 快捷格 + 大按钮
 */
#ifndef UI_MAIN_H
#define UI_MAIN_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 创建主界面并返回其对象（永久屏，只创建一次） */
lv_obj_t *ui_main_create(void);

/** 更新「最近一次结果」那一行；传 NULL 或空串则显示占位文案 */
void ui_main_set_result(const char *text);

/** 更新时钟与日期（由 RTC 定时器每秒调用一次） */
void ui_main_set_time(const char *time_str, const char *date_str);

#ifdef __cplusplus
}
#endif

#endif /* UI_MAIN_H */
