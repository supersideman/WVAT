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

/** 创建主界面并返回其对象（独立屏幕版，一般不用：开机走 ui_stage）*/
lv_obj_t *ui_main_create(void);

/** 把主界面内容画进指定容器（ui_stage 用它做"跟手滑动"的一层）*/
void ui_main_build(lv_obj_t *parent);

/**
 * 意图回调：用户点了某个意图格 / 麦克风按钮时被调用
 *  - 未注册：UI 只在本地更新回执条（无链路也能演示）
 *  - 注册后：交给链路层发指令，等 PC 回执再更新回执条
 */
typedef void (*ui_intent_cb_t)(ui_icon_t kind);
void ui_main_set_intent_cb(ui_intent_cb_t cb);

/** 取当前注册的意图回调（抽屉页要用；未注册返回 NULL） */
ui_intent_cb_t ui_main_get_intent_cb(void);

/**
 * 文本回调：录音页确认发送时，把"用户确认过的文字"交给链路层。
 * 链路层用 OV+AI=<id>,T,<文字> 发出去（慢通道，由网关理解并执行）。
 */
typedef void (*ui_text_cb_t)(const char *text);
void ui_main_set_text_cb(ui_text_cb_t cb);
ui_text_cb_t ui_main_get_text_cb(void);

/**
 * 录音控制回调：开始 / 结束录音。
 * 用电脑麦克风兜底时，必须告诉网关"现在开始录"和"现在停止并转写"——
 * 等按下停止才开始录的话，用户说的话已经过去了，录不到。
 */
typedef void (*ui_mic_cb_t)(bool start);
void ui_main_set_mic_cb(ui_mic_cb_t cb);
ui_mic_cb_t ui_main_get_mic_cb(void);

/* 注：切页统一走 ui_stage（ui_stage_show_home / ui_stage_lock），
   所以这里不再暴露 ui_main_show()。 */

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
