/**
 * @file    ui_settings.c
 * @brief   设置页 · LVGL 8.2
 *
 * 入口：主界面底部页面条（点击或向左滑）
 * 出口：顶部返回键 / 底部页面条（点击或向右滑）
 *
 * 设计延续整个项目的一条原则：**界面不放中文**
 *   → 每行一个 3 字母 ASCII 标签 + 右侧数值/图形，零字库成本。
 *
 * 五行内容：
 *   BRI   亮度（5 档：20/40/60/80/100，点一下升一档，到顶回底）
 *   BT    蓝牙开关（关掉会真的 DeInit USART2，省电也便于排查）
 *   LINK  当前活跃链路（U=USB / B=蓝牙 / -=无）——打开页面时取快照
 *   CLR   清空语音历史（点一下清空，显示 DONE 1.2 秒）
 *   VER   固件版本（来自 ui_version.h，与 git tag 对齐）
 */
#include "ui_settings.h"

#include <stdio.h>

#include "lvgl.h"
#include "ui_history.h"
#include "ui_icons.h"
#include "ui_stage.h"
#include "ui_theme.h"
#include "ui_version.h"
#include "user_backlight.h"
#include "user_link.h"

#define COL(c) lv_color_hex(c)

#define BRI_LEVELS       5
#define BRI_SEG_W        16
#define BRI_SEG_H        8
#define BRI_SEG_GAP      6
#define BRI_SEG_GROUP_W  (BRI_LEVELS * BRI_SEG_W + (BRI_LEVELS - 1) * BRI_SEG_GAP)

/* 亮度档位：20~100。不设 0——整屏黑掉容易被误认为死机。 */
static const uint8_t k_bri[BRI_LEVELS] = {20, 40, 60, 80, 100};

static lv_obj_t *s_scr     = NULL;
static bool      s_visible = false;
static int       s_bri_idx = BRI_LEVELS - 1;

static lv_obj_t  *s_seg[BRI_LEVELS];
static lv_obj_t  *s_lbl_bt   = NULL;
static lv_obj_t  *s_lbl_link = NULL;
static lv_obj_t  *s_lbl_clr  = NULL;
static lv_obj_t  *s_lbl_ver  = NULL;
static lv_timer_t *s_clr_timer = NULL;

/* ───────────────── 工具 ───────────────── */

static lv_obj_t *sbox(lv_obj_t *parent, int x, int y, int w, int h, int radius,
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

static lv_obj_t *slabel(lv_obj_t *parent, int x, int y, int w, uint32_t color,
                        lv_text_align_t align, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, COL(color), 0);
    lv_obj_set_style_text_align(l, align, 0);
    lv_label_set_text(l, txt);
    return l;
}

static void pressable(lv_obj_t *o, lv_event_cb_t cb)
{
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(o, COL(UI_C_PRESSED), LV_STATE_PRESSED);
    if (cb) lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, NULL);
}

/** 一行：卡片 + 左侧 3 字母标签，返回卡片对象供挂点击 */
static lv_obj_t *row(lv_obj_t *parent, int idx, const char *tag)
{
    int y = UI_SET_ROW_Y0 + idx * UI_SET_ROW_STEP;
    lv_obj_t *card = sbox(parent, UI_SET_ROW_X, y, UI_SET_ROW_W, UI_SET_ROW_H,
                          10, UI_C_CARD, LV_OPA_COVER);
    slabel(card, 12, (UI_SET_ROW_H - 16) / 2, 60, UI_C_DIM,
           LV_TEXT_ALIGN_LEFT, tag);
    return card;
}

/* ───────────────── 刷新 ───────────────── */

static void refresh_brightness(void)
{
    for (int i = 0; i < BRI_LEVELS; i++) {
        int lit = (i <= s_bri_idx);
        if (!s_seg[i]) continue;
        lv_obj_set_style_bg_color(s_seg[i], COL(lit ? UI_C_ACCENT : UI_C_DIM), 0);
        lv_obj_set_style_bg_opa(s_seg[i], lit ? LV_OPA_COVER : LV_OPA_30, 0);
    }
}

static void refresh_bt(void)
{
    if (!s_lbl_bt) return;
    bool on = link_bt_is_enabled();
    lv_label_set_text(s_lbl_bt, on ? "ON" : "OFF");
    lv_obj_set_style_text_color(s_lbl_bt, COL(on ? UI_C_ACCENT : UI_C_DIM), 0);
}

static void refresh_link(void)
{
    if (!s_lbl_link) return;
    int kind = link_active_kind();          /* 0=无 1=USB 2=蓝牙 */
    const char *txt = "-";
    uint32_t    col = UI_C_DIM;
    if (kind == 1) { txt = "U"; col = UI_C_ACCENT; }
    else if (kind == 2) { txt = "B"; col = UI_C_INFO; }
    lv_label_set_text(s_lbl_link, txt);
    lv_obj_set_style_text_color(s_lbl_link, COL(col), 0);
}

static void refresh_all(void)
{
    refresh_brightness();
    refresh_bt();
    refresh_link();
    if (s_lbl_clr) lv_label_set_text(s_lbl_clr, "TAP");
    if (s_lbl_ver) lv_label_set_text(s_lbl_ver, "v" OV_FW_VERSION);
}

/* ───────────────── 交互 ───────────────── */

static void on_bri_clicked(lv_event_t *e)
{
    (void)e;
    s_bri_idx = (s_bri_idx + 1) % BRI_LEVELS;    /* 20→40→…→100→20 */
    bl_set_percent(k_bri[s_bri_idx]);
    refresh_brightness();
}

static void on_bt_clicked(lv_event_t *e)
{
    (void)e;
    link_bt_set_enabled(!link_bt_is_enabled());
    refresh_bt();
}

static void clr_timer_cb(lv_timer_t *t)
{
    (void)t;
    s_clr_timer = NULL;                   /* repeat_count=1，LVGL 自己会删 */
    if (s_lbl_clr) lv_label_set_text(s_lbl_clr, "TAP");
}

static void on_clr_clicked(lv_event_t *e)
{
    (void)e;
    ui_history_clear();
    if (s_lbl_clr) {
        lv_label_set_text(s_lbl_clr, "DONE");
        lv_obj_set_style_text_color(s_lbl_clr, COL(UI_C_ACCENT), 0);
    }
    if (s_clr_timer) lv_timer_del(s_clr_timer);
    s_clr_timer = lv_timer_create(clr_timer_cb, 1200, NULL);
    lv_timer_set_repeat_count(s_clr_timer, 1);
}

/* 顶部返回键 / 底部页面条：都回到主界面 */
static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    ui_settings_close();
}

/* 整层背景向右滑 → 返回主界面（与主界面"向左滑进设置"对称）。
   和主界面同一套思路：标签不可点，落到背景上的触点由这一层收；
   落在行卡片/返回键上的触点先被它们拿走，不会误翻页。 */
static int s_press_x = -1;

static void on_scr_pressed(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_PRESSED) return;
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    s_press_x = p.x;
}

static void on_scr_released(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_RELEASED) return;
    if (s_press_x < 0) return;

    int dx = 0;
    lv_indev_t *indev = lv_indev_get_act();
    if (indev) {
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        dx = p.x - s_press_x;
    }
    s_press_x = -1;

    if (dx >= UI_PAGE_SWIPE_PX) {          /* 向右滑 → 回主界面 */
        ui_settings_close();
    }
}

/* ───────────────── 构建 ───────────────── */

static void build(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_scr);
    lv_obj_set_size(s_scr, UI_W, UI_H);
    lv_obj_set_style_bg_color(s_scr, COL(UI_C_BG), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* 顶部：返回键 + 标题 */
    lv_obj_t *back = sbox(s_scr, UI_SET_BACK_X, UI_SET_BACK_Y,
                          UI_SET_BACK_SIZE, UI_SET_BACK_SIZE, 8,
                          UI_C_CARD, LV_OPA_COVER);
    ui_icon_draw(back, UI_ICON_BACK, UI_SET_BACK_SIZE / 2, UI_SET_BACK_SIZE / 2,
                 UI_SET_BACK_SIZE - 2, UI_C_TEXT);
    pressable(back, on_back_clicked);

    slabel(s_scr, 0, 8, UI_W, UI_C_TEXT, LV_TEXT_ALIGN_CENTER, "SETTINGS");

    /* 行 0：亮度 */
    lv_obj_t *r0 = row(s_scr, 0, "BRI");
    for (int i = 0; i < BRI_LEVELS; i++) {
        int sx = UI_SET_ROW_W - 12 - BRI_SEG_GROUP_W + i * (BRI_SEG_W + BRI_SEG_GAP);
        s_seg[i] = sbox(r0, sx, (UI_SET_ROW_H - BRI_SEG_H) / 2,
                        BRI_SEG_W, BRI_SEG_H, 3, UI_C_DIM, LV_OPA_30);
    }
    pressable(r0, on_bri_clicked);

    /* 行 1：蓝牙开关 */
    lv_obj_t *r1 = row(s_scr, 1, "BT");
    s_lbl_bt = slabel(r1, UI_SET_ROW_W - 12 - 60, (UI_SET_ROW_H - 16) / 2, 60,
                      UI_C_DIM, LV_TEXT_ALIGN_RIGHT, "OFF");
    pressable(r1, on_bt_clicked);

    /* 行 2：当前链路（只读）*/
    lv_obj_t *r2 = row(s_scr, 2, "LINK");
    s_lbl_link = slabel(r2, UI_SET_ROW_W - 12 - 60, (UI_SET_ROW_H - 16) / 2, 60,
                        UI_C_DIM, LV_TEXT_ALIGN_RIGHT, "-");

    /* 行 3：清空历史 */
    lv_obj_t *r3 = row(s_scr, 3, "CLR");
    s_lbl_clr = slabel(r3, UI_SET_ROW_W - 12 - 60, (UI_SET_ROW_H - 16) / 2, 60,
                       UI_C_DIM, LV_TEXT_ALIGN_RIGHT, "TAP");
    pressable(r3, on_clr_clicked);

    /* 行 4：版本（只读）*/
    lv_obj_t *r4 = row(s_scr, 4, "VER");
    s_lbl_ver = slabel(r4, UI_SET_ROW_W - 12 - 90, (UI_SET_ROW_H - 16) / 2, 90,
                       UI_C_TEXT, LV_TEXT_ALIGN_RIGHT, "v" OV_FW_VERSION);

    /* 底部页面条：两个点（第 2 页高亮）+ 左侧 "<" 提示 */
    lv_obj_t *bar = sbox(s_scr, 0, UI_PAGEBAR_Y, UI_W, UI_PAGEBAR_H, 0,
                         UI_C_BG, LV_OPA_TRANSP);
    lv_obj_t *lft = slabel(bar, 14, (UI_PAGEBAR_H - 16) / 2, 20, UI_C_DIM,
                           LV_TEXT_ALIGN_LEFT, "<");
    (void)lft;
    int cx = UI_W / 2;
    sbox(bar, cx - UI_PAGEDOT_GAP / 2 - UI_PAGEDOT_SZ,
         (UI_PAGEBAR_H - UI_PAGEDOT_SZ) / 2, UI_PAGEDOT_SZ, UI_PAGEDOT_SZ,
         UI_PAGEDOT_SZ / 2, UI_C_DIM, LV_OPA_COVER);
    sbox(bar, cx + UI_PAGEDOT_GAP / 2,
         (UI_PAGEBAR_H - UI_PAGEDOT_SZ) / 2, UI_PAGEDOT_SZ, UI_PAGEDOT_SZ,
         UI_PAGEDOT_SZ / 2, UI_C_ACCENT, LV_OPA_COVER);
    pressable(bar, on_back_clicked);

    /* 整层背景：向右滑返回（放最后，只兜住没被行卡片接走的触点）*/
    lv_obj_add_flag(s_scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_scr, on_scr_pressed,  LV_EVENT_PRESSED,  NULL);
    lv_obj_add_event_cb(s_scr, on_scr_released, LV_EVENT_RELEASED, NULL);
}

/* ───────────────── 对外接口 ───────────────── */

void ui_settings_open(void)
{
    if (!s_scr) build();
    refresh_all();
    lv_scr_load_anim(s_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
    s_visible = true;
}

void ui_settings_close(void)
{
    if (!s_visible) return;
    s_visible = false;
    ui_stage_show_home(LV_SCR_LOAD_ANIM_MOVE_RIGHT);
}

bool ui_settings_visible(void)
{
    return s_visible;
}
