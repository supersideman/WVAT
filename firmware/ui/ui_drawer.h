/**
 * @file    ui_drawer.h
 * @brief   抽屉页：把 8 个意图摊开成 3×3（第 9 格是返回）
 *
 * 只负责"把意图抛出去"，不关心它怎么被执行——具体走串口还是本地，
 * 由 ui_main 注册的意图回调决定。
 */
#ifndef UI_DRAWER_H
#define UI_DRAWER_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 创建抽屉页（只创建一次，返回其 screen 对象） */
lv_obj_t *ui_drawer_create(void);

/** 从主界面滑入抽屉页 */
void ui_drawer_open(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_DRAWER_H */
