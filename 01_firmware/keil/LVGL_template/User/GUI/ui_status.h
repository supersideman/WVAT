/**
 * @file    ui_status.h
 * @brief   状态页：全屏反馈（录音中 / 成功 / 失败），服务于语音流程
 *
 * 为什么单独做一个全屏页，而不是复用抽屉的小圆点：
 *   小圆点适合"点一下就执行完"的快速操作；
 *   而语音从"按下说话"到"识别出结果"有明显的过程感，
 *   需要一屏只说明一件事的强反馈。
 *
 * 生命周期：
 *   ui_status_recording()  → 显示录音中（红波形）
 *   ui_status_show_result()→ 显示成功/失败，1.2 秒后自动返回上一页
 *   ui_status_hide()       → 手动返回
 */
/* ⚠️ 这里的 guard 原名是 UI_STATUS_H，与 ui_theme.h 里的布局常量
   `#define UI_STATUS_H 44`（顶部状态区高度）重名。
   谁先 include 谁生效：ui_main.c 先包含 ui_theme.h，于是本文件整个被跳过，
   ui_status_* 全部变成"隐式声明"——ARMCC 只警告不报错，潜伏了很久。
   所以 guard 改成 UI_STATUS_PAGE_H。 */
#ifndef UI_STATUS_PAGE_H
#define UI_STATUS_PAGE_H

#include <stdbool.h>

#include "lvgl.h"
#include "ui_icons.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 显示"录音中"（红波形）。记住进入前的界面，用于返回 */
void ui_status_recording(void);

/** 显示结果（大图标 + 外圈），ms 毫秒后自动返回上一页 */
void ui_status_show_result(ui_icon_t kind, bool ok, uint32_t ms);

/** 立即返回进入状态页之前的界面 */
void ui_status_hide(void);

/** 状态页当前是否可见（用于判断回执要不要走全屏反馈） */
bool ui_status_visible(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_STATUS_PAGE_H */
