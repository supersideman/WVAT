/**
 * @file    ui_drawer.h
 * @brief   抽屉页：只放拓展指令（3 列 × 2 行 = 6 格）
 *
 * 顶部条：左=返回键，右=操作反馈（意图图标 + ✓/✗）。
 * 点指令后**留在本页**，不回主界面。
 */
#ifndef UI_DRAWER_H
#define UI_DRAWER_H

#include <stdbool.h>

#include "lvgl.h"
#include "ui_icons.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 创建抽屉页（只创建一次，返回其 screen 对象） */
lv_obj_t *ui_drawer_create(void);

/** 从主界面滑入抽屉页 */
void ui_drawer_open(void);

/** 回执到达：更新顶部条右侧的小反馈（抽屉未创建时忽略）*/
void ui_drawer_set_result(ui_icon_t kind, bool ok);

#ifdef __cplusplus
}
#endif

#endif /* UI_DRAWER_H */
