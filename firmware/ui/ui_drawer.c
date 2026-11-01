/**
 * @file    ui_drawer.c
 * @brief   抽屉页实现 · LVGL 8.2
 */
#include "ui_drawer.h"

#include <stdint.h>

#include "ui_icons.h"
#include "ui_main.h"
#include "ui_theme.h"

#define COL(c) lv_color_hex(c)

static lv_obj_t *s_drawer = NULL;

/* ───────────────── 工具（与 ui_main 同款风格）───────────────── */

static lv_obj_t *dbox(lv_obj_t *parent, int x, int y, int w, int h, int radius,
                      uint32_t color, lv_opa_t opa)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, COL(color), 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static void dpressable(lv_obj_t *o, lv_event_cb_t cb, void *user_data)
{
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(o, COL(UI_C_PRESSED), LV_STATE_PRESSED);
    if (cb) lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, user_data);
}

/* ───────────────── 事件 ───────────────── */

/** 点了某个意图格：交给链路，然后回主界面（好让回执条可见） */
static void on_intent_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    ui_icon_t kind = (ui_icon_t)(intptr_t)lv_event_get_user_data(e);
    ui_intent_cb_t cb = ui_main_get_intent_cb();

    if (cb) {
        cb(kind);                 /* 真机：发 OV+AI=，等回执 */
    } else {
        ui_main_set_result(kind, true);   /* 无链路：本地假回执 */
    }
    ui_main_show(LV_SCR_LOAD_ANIM_MOVE_RIGHT);   /* 回主界面看结果 */
}

/** 点了返回 */
static void on_back_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_main_show(LV_SCR_LOAD_ANIM_MOVE_RIGHT);
}

/* ───────────────── 构建 ───────────────── */

typedef struct {
    ui_icon_t icon;
    uint32_t  color;
} drw_item_t;

/* 8 个意图，正好铺满 3×3 的前 8 格，第 9 格放返回 */
static const drw_item_t k_items[8] = {
    { UI_ICON_MUSIC,  UI_C_TEXT   },   /* A01 放歌/暂停 */
    { UI_ICON_NEXT,   UI_C_TEXT   },   /* A02 下一首   */
    { UI_ICON_MOON,   UI_C_TEXT   },   /* A03 我困了   */
    { UI_ICON_VOL,    UI_C_TEXT   },   /* A04 音量+    */
    { UI_ICON_MUTE,   UI_C_TEXT   },   /* A05 音量-    */
    { UI_ICON_FOLDER, UI_C_TEXT   },   /* A06 打开资料 */
    { UI_ICON_LIST,   UI_C_TEXT   },   /* A07 查状态   */
    { UI_ICON_MIC,    UI_C_ACCENT },   /* A08 语音     */
};

lv_obj_t *ui_drawer_create(void)
{
    if (s_drawer) return s_drawer;

    s_drawer = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_drawer);
    lv_obj_set_style_bg_color(s_drawer, COL(UI_C_BG), 0);
    lv_obj_set_style_bg_opa(s_drawer, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_drawer, LV_OBJ_FLAG_SCROLLABLE);

    /* 顶部细条：给视觉一个"这是另一页"的锚点（不放文字）*/
    dbox(s_drawer, 0, 0, UI_W, UI_DRW_TOP_H, 0, UI_C_CARD, LV_OPA_COVER);
    ui_icon_draw(s_drawer, UI_ICON_GRID, 22, UI_DRW_TOP_H / 2, 22, UI_C_ACCENT);

    /* 3×3：8 个意图 + 1 个返回 */
    for (int i = 0; i < 9; i++) {
        int col = i % 3;
        int row = i / 3;
        int x = UI_DRW_X0 + col * UI_DRW_STEP_X;
        int y = UI_DRW_Y0 + row * UI_DRW_STEP_Y;

        lv_obj_t *card = dbox(s_drawer, x, y, UI_DRW_W, UI_DRW_H,
                              UI_DRW_RADIUS, UI_C_CARD, LV_OPA_COVER);

        if (i < 8) {
            ui_icon_draw(card, k_items[i].icon,
                         UI_DRW_W / 2, UI_DRW_ICON_CY,
                         UI_DRW_ICON_SZ, k_items[i].color);
            dpressable(card, on_intent_clicked, (void *)(intptr_t)k_items[i].icon);
        } else {
            /* 第 9 格：返回 */
            ui_icon_draw(card, UI_ICON_BACK,
                         UI_DRW_W / 2, UI_DRW_ICON_CY,
                         UI_DRW_ICON_SZ, UI_C_DIM);
            dpressable(card, on_back_clicked, NULL);
        }
    }

    return s_drawer;
}

void ui_drawer_open(void)
{
    lv_obj_t *scr = ui_drawer_create();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}
