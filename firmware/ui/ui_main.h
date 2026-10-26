/**
 * @file    ui_main.h
 * @brief   主界面（图形化版）：时钟 + 日期 + 结果回执 + 3 意图格 + 麦克风按钮
 */
#ifndef UI_MAIN_H
#define UI_MAIN_H

#include <stdbool.h>
#include "lvgl.h"
#include "ui_icons.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 创建主界面并返回其对象（永久屏，只创建一次） */
lv_obj_t *ui_main_create(void);

/**
 * 更新「最近一次结果」回执（纯图标：意图图标 + 对勾/叉）
 * @param intent 刚执行完的意图图标
 * @param ok     true=成功（青色勾），false=失败（红色叉）
 */
void ui_main_set_result(ui_icon_t intent, bool ok);

/**
 * 更新时钟与日期
 * @param time_str "10:24"（数字+冒号，内置字体即可）
 * @param date_str "10/20"（纯数字，避免中文字库）
 */
void ui_main_set_time(const char *time_str, const char *date_str);

#ifdef __cplusplus
}
#endif

#endif /* UI_MAIN_H */
