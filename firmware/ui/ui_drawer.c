/**
 * @file    ui_drawer.c
 * @brief   抽屉页实现 · LVGL 8.2
 *
 * 设计约定（v5）
 * - 主界面已经有「播放/暂停」「下一首」，抽屉只放**拓展指令**，不重复
 * - 抽屉为 3 列 × 2 行 = 6 格
 * - 点指令后**留在抽屉**，不跳回主界面；反馈显示在顶部条右侧的小槽里
 * - 返回键固定在顶部条左侧
 */
#include "ui_drawer.h"

#include <stdint.h>

#include "ui_icons.h"
#include "ui_main.h"
#include "ui_stage.h"
#include "ui_theme.h"

#define COL(c) lv_color_hex(c)

#define FB_ICON_SZ UI_DRW_FB_ICON_SZ

static lv_obj_t *s_drawer   = NULL;
static lv_obj_t *s_fb_intent = NULL;   /* 顶部反馈：刚点的意图图标 */
static lv_obj_t *s_fb_dot    = NULL;   /* 顶部反馈：结果圆点（绿/红）*/

/* 超时守卫：点了指令但没等到回执（网关没开/串口掉了/指令丢了），
   3 秒后显示 ✗——否则用户只看到"一直没反应"，无从判断是没执行还是没回复。 */
static bool      s_fb_waiting = false;
static uint32_t  s_fb_tick    = 0;
static ui_icon_t s_fb_kind    = UI_ICON_MOON;

/* ───────────────── 工具 ───────────────── */

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

/**
 * 可点击 + 有按下反馈
 * ⚠️ 一定要给常态 bg_opa（哪怕跟背景同色），否则按下态只改颜色、透明度还是 0，
 *    看起来就是"没反馈"——返回键之前就踩了这个坑。
 */
static void dpressable(lv_obj_t *o, lv_event_cb_t cb, void *user_data)
{
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, COL(UI_C_CARD), 0);            /* 与顶部条同色，平时看不出 */
    lv_obj_set_style_bg_color(o, COL(UI_C_PRESSED), LV_STATE_PRESSED);
    if (cb) lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, user_data);
}

/** 透明的图标槽：重画时只需 lv_obj_clean(槽) 再画图标 */
static void slot(lv_obj_t **out, lv_obj_t *parent, int x, int y, int size)
{
    *out = dbox(parent, x, y, size, size, 0, UI_C_CARD, LV_OPA_TRANSP);
}

/* ───────────────── 顶部反馈 ───────────────── */

/**
 * 重画顶部小反馈：左边意图图标，右边一个结果圆点
 * state: -1 等待（无点）、0 失败（红点）、1 成功（绿点）
 */
static void fb_repaint(ui_icon_t kind, int state)
{
    if (!s_fb_intent) return;
    lv_obj_clean(s_fb_intent);

    ui_icon_draw(s_fb_intent, kind, FB_ICON_SZ / 2, FB_ICON_SZ / 2,
                 FB_ICON_SZ, state < 0 ? UI_C_DIM : UI_C_TEXT);

    if (!s_fb_dot) return;
    if (state < 0) {
        lv_obj_set_style_bg_opa(s_fb_dot, LV_OPA_TRANSP, 0);   /* 等待中：不显示点 */
    } else {
        lv_obj_set_style_bg_color(s_fb_dot,
                                  COL(state ? UI_C_ACCENT : UI_C_DANGER), 0);
        lv_obj_set_style_bg_opa(s_fb_dot, LV_OPA_COVER, 0);
    }
}

/** 超时守卫：常驻 200ms 轮询，靠 flag 判断，避免反复建/删定时器 */
static void fb_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_fb_waiting) return;
    if (lv_tick_elaps(s_fb_tick) < UI_DRW_FB_TIMEOUT_MS) return;

    s_fb_waiting = false;
    fb_repaint(s_fb_kind, 0);        /* 超时 → 按失败显示红叉 */
}

/* ───────────────── 事件 ───────────────── */

/** 点了某个意图格：发出去、留在本页，顶部先显示"等待"，回执到了再变 ✓/✗ */
static void on_intent_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    ui_icon_t kind = (ui_icon_t)(intptr_t)lv_event_get_user_data(e);
    ui_intent_cb_t cb = ui_main_get_intent_cb();

    s_fb_kind    = kind;
    s_fb_tick    = lv_tick_get();
    s_fb_waiting = true;

    fb_repaint(kind, -1);            /* 立刻给"已受理"的视觉反馈 */

    if (cb) {
        cb(kind);                    /* 真机：发 OV+AI=，等回执 */
    } else {
        ui_main_set_result(kind, true);   /* 无链路：本地假回执（会回调到这里）*/
    }
    /* 注意：**不切页**，留在抽屉里继续操作 */
}

/** 点了返回 */
static void on_back_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_stage_show_home(LV_SCR_LOAD_ANIM_MOVE_RIGHT);
}

/* ───────────────── 构建 ───────────────── */

typedef struct {
    ui_icon_t icon;
    uint32_t  color;
} drw_item_t;

/* 6 个拓展指令（主界面已占用 A01 播放/暂停、A02 下一首）*/
static const drw_item_t k_items[6] = {
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
    lv_obj_set_size(s_drawer, UI_W, UI_H);
    lv_obj_set_style_bg_color(s_drawer, COL(UI_C_BG), 0);
    lv_obj_set_style_bg_opa(s_drawer, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_drawer, LV_OBJ_FLAG_SCROLLABLE);

    /* ── 顶部条：左返回、右反馈 ── */
    dbox(s_drawer, 0, 0, UI_W, UI_DRW_TOP_H, 0, UI_C_CARD, LV_OPA_COVER);

    /* 返回键：常态底色=顶部条同色（看不出），按下变亮一档 → 有明确反馈 */
    lv_obj_t *back = dbox(s_drawer, UI_BACK_X, UI_BACK_Y,
                          UI_BACK_SIZE, UI_BACK_SIZE, 8,
                          UI_C_CARD, LV_OPA_COVER);
    ui_icon_draw(back, UI_ICON_BACK, UI_BACK_SIZE / 2, UI_BACK_SIZE / 2,
                 UI_BACK_SIZE - 2, UI_C_TEXT);
    dpressable(back, on_back_clicked, NULL);

    slot(&s_fb_intent, s_drawer, UI_DRW_FB_X, UI_DRW_FB_Y, FB_ICON_SZ);

    /* 结果圆点：默认透明（等待中不显示），有结果才点亮 */
    s_fb_dot = dbox(s_drawer, UI_DRW_DOT_X, UI_DRW_DOT_Y,
                    UI_DRW_DOT_SZ, UI_DRW_DOT_SZ, LV_RADIUS_CIRCLE,
                    UI_C_ACCENT, LV_OPA_TRANSP);

    /* 超时守卫（常驻，200ms 一次）*/
    lv_timer_create(fb_timer_cb, 200, NULL);

    /* ── 3×2 指令格 ── */
    for (int i = 0; i < 6; i++) {
        int col = i % 3;
        int row = i / 3;
        int x = UI_DRW_X0 + col * UI_DRW_STEP_X;
        int y = UI_DRW_Y0 + row * UI_DRW_STEP_Y;

        lv_obj_t *card = dbox(s_drawer, x, y, UI_DRW_W, UI_DRW_H,
                              UI_DRW_RADIUS, UI_C_CARD, LV_OPA_COVER);
        ui_icon_draw(card, k_items[i].icon, UI_DRW_W / 2, UI_DRW_ICON_CY,
                     UI_DRW_ICON_SZ, k_items[i].color);
        dpressable(card, on_intent_clicked, (void *)(intptr_t)k_items[i].icon);
    }

    return s_drawer;
}

void ui_drawer_set_result(ui_icon_t kind, bool ok)
{
    if (!s_drawer) return;              /* 抽屉还没建过，忽略 */
    s_fb_waiting = false;               /* 回执到了，撤销超时守卫 */
    fb_repaint(kind, ok ? 1 : 0);
}

void ui_drawer_open(void)
{
    lv_obj_t *scr = ui_drawer_create();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}
