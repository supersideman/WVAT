/**
 * @file    ui_settings.h
 * @brief   设置页（独立屏幕）：亮度 / 蓝牙 / 链路 / 清空历史 / 版本
 */
#ifndef UI_SETTINGS_H
#define UI_SETTINGS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 打开设置页（从主界面底部页面条进入，向左滑动动画）*/
void ui_settings_open(void);

/** 关闭设置页（回到主界面，向右滑动动画）*/
void ui_settings_close(void);

/** 设置页是否正开着 */
bool ui_settings_visible(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_SETTINGS_H */
