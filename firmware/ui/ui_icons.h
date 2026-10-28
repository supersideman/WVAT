/**
 * @file    ui_icons.h
 * @brief   纯图元图标库（零位图、零字库）
 *
 * 全部用 LVGL 原生对象拼（圆 / 圆角矩形），所以：
 *   - 不占 Flash（没有位图数组）
 *   - 不占额外 RAM（对象本来就存在）
 *   - 颜色可随时改（跟随主题）
 *
 * 图标库里每个函数都按「中心点 + 半径量级」绘制，
 * 想加大减小只改 size 参数，不用改坐标。
 */
#ifndef UI_ICONS_H
#define UI_ICONS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 图标种类 */
typedef enum {
    UI_ICON_MUSIC = 0,   /* ♪ 两个符头 */
    UI_ICON_MOON,        /* 月牙 */
    UI_ICON_FOLDER,      /* 文件夹 */
    UI_ICON_GRID,        /* 田字格（更多）*/
    UI_ICON_MIC,         /* 麦克风 */
    UI_ICON_NEXT,        /* 下一个 */
    UI_ICON_VOL,         /* 音量 */
    UI_ICON_MUTE,        /* 静音 */
    UI_ICON_LIST,        /* 列表（状态）*/
    UI_ICON_GEAR,        /* 齿轮（设置）*/
    UI_ICON_OK,          /* 对勾 */
    UI_ICON_CROSS,       /* 叉 */
} ui_icon_t;

/**
 * 在 parent 上画一个图标
 * @param parent  父对象
 * @param kind    图标种类
 * @param cx,cy   图标中心（相对父对象）
 * @param size    图标整体尺寸（像素，建议 24~34）
 * @param color   颜色
 */
void ui_icon_draw(lv_obj_t *parent, ui_icon_t kind, int cx, int cy, int size,
                  uint32_t color);

#ifdef __cplusplus
}
#endif

#endif /* UI_ICONS_H */
