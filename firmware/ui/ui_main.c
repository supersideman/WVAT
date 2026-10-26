/**
 * @file    ui_main.c
 * @brief   主界面（B 版）实现 · LVGL 8.2
 *
 * 设计要点
 * - 所有坐标来自 ui_theme.h，屏幕尺寸来自 UI_W / UI_H（不写死 240/280 之外的东西）
 * - 图标用 LVGL 原生图元拼（圆/圆角矩形），**不占 Flash、不用位图**
 * - 只有一个 lv_label 会频繁更新（时钟、结果），其余静态
 * - 对象只创建一次；页面切换用 hidden 标志，不反复 create/delete（防堆碎片）
 */
#include "ui_main.h"
#include "ui_theme.h"

#define COL(c) lv_color_hex(c)

static lv_obj_t *s_scr;         /* 屏幕根 */
static lv_obj_t *s_lbl_time;
static lv_obj_t *s_lbl_date;
static lv_obj_t *s_lbl_result;

/* ───────────────────────── 小工具 ───────────────────────── */

/** 一个纯色圆角块（去掉默认主题的 padding/border，坐标才可控） */
static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, int radius,
                     uint32_t color, lv_opa_t opa)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);                 /* 清掉默认样式，避免主题干扰 */
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, COL(color), 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

/** 圆形点 / 圆环 */
static lv_obj_t *dot(lv_obj_t *parent, int cx, int cy, int r,
                     uint32_t color, int border_w)
{
    lv_obj_t *o = box(parent, cx - r, cy - r, r * 2, r * 2, LV_RADIUS_CIRCLE, color,
                      border_w > 0 ? LV_OPA_TRANSP : LV_OPA_COVER);
    if (border_w > 0) {
        lv_obj_set_style_border_color(o, COL(color), 0);
        lv_obj_set_style_border_width(o, border_w, 0);
    }
    return o;
}

static lv_obj_t *label(lv_obj_t *parent, int x, int y, int w,
                       const lv_font_t *font, uint32_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, x, y);
    if (w > 0) lv_obj_set_width(l, w);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, COL(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, txt);
    return l;
}

/* ───────────────────────── 四个图标 ───────────────────────── */
/* 全部用图元拼，颜色跟随父卡片 */

static void icon_music(lv_obj_t *p, int cx, int cy)
{
    /* 两个符头 + 两根符干 + 一根横梁，近似 ♪♪ */
    dot(p, cx - 6, cy + 6, 4, UI_C_TEXT, 0);
    dot(p, cx + 5, cy + 3, 4, UI_C_TEXT, 0);
    box(p, cx - 3, cy - 8, 2, 14, 0, UI_C_TEXT, LV_OPA_COVER);
    box(p, cx + 8, cy - 11, 2, 14, 0, UI_C_TEXT, LV_OPA_COVER);
    box(p, cx - 3, cy - 9, 13, 2, 0, UI_C_TEXT, LV_OPA_COVER);
}

static void icon_moon(lv_obj_t *p, int cx, int cy)
{
    /* 实心圆 + 卡片底色圆挖缺 → 月牙 */
    dot(p, cx, cy, 8, UI_C_TEXT, 0);
    dot(p, cx + 5, cy - 3, 8, UI_C_CARD, 0);
}

static void icon_folder(lv_obj_t *p, int cx, int cy)
{
    box(p, cx - 10, cy - 7, 20, 14, 2, UI_C_TEXT, LV_OPA_COVER);      /* 主体 */
    box(p, cx - 10, cy - 10, 9, 4, 1, UI_C_TEXT, LV_OPA_COVER);       /* 折页 */
}

static void icon_grid(lv_obj_t *p, int cx, int cy, uint32_t color)
{
    /* 田字格 = 4 个小方块 */
    box(p, cx - 8, cy - 8, 6, 6, 2, color, LV_OPA_COVER);
    box(p, cx + 2, cy - 8, 6, 6, 2, color, LV_OPA_COVER);
    box(p, cx - 8, cy + 2, 6, 6, 2, color, LV_OPA_COVER);
    box(p, cx + 2, cy + 2, 6, 6, 2, color, LV_OPA_COVER);
}

/** 一个快捷格：图标 + 文字 */
static void tile(lv_obj_t *parent, int x, const char *txt, int kind, uint32_t txt_color)
{
    lv_obj_t *card = box(parent, x, UI_TILE_Y, UI_TILE_SIZE, UI_TILE_SIZE,
                         UI_TILE_RADIUS, UI_C_CARD, LV_OPA_COVER);
    int cx = UI_TILE_SIZE / 2;
    int cy = 20;

    switch (kind) {
    case 0: icon_music(card, cx, cy);            break;
    case 1: icon_moon(card, cx, cy);             break;
    case 2: icon_folder(card, cx, cy);           break;
    default: icon_grid(card, cx, cy, txt_color); break;
    }

    label(card, 0, 32, UI_TILE_SIZE, UI_FONT_CN12, txt_color, txt);
}

/* ───────────────────────── 对外接口 ───────────────────────── */

lv_obj_t *ui_main_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_scr);
    lv_obj_set_style_bg_color(s_scr, COL(UI_C_BG), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* 时钟 */
    s_lbl_time = label(s_scr, 0, UI_TIME_Y, UI_W, UI_FONT_NUM44, UI_C_TEXT, "10:24");

    /* 日期 */
    s_lbl_date = label(s_scr, 0, UI_DATE_Y, UI_W, UI_FONT_CN12, UI_C_DIM, "10月20日 周一");

    /* 最近一次结果 */
    lv_obj_t *res = box(s_scr, UI_RESULT_X, UI_RESULT_Y, UI_RESULT_W, UI_RESULT_H,
                        UI_RESULT_RADIUS, UI_C_CARD, LV_OPA_COVER);
    dot(res, 8, UI_RESULT_H / 2, 4, UI_C_ACCENT, 0);
    s_lbl_result = lv_label_create(res);
    lv_obj_set_pos(s_lbl_result, 16, 7);
    lv_obj_set_style_text_font(s_lbl_result, UI_FONT_CN12, 0);
    lv_obj_set_style_text_color(s_lbl_result, COL(UI_C_DIM), 0);
    lv_label_set_text(s_lbl_result, "上次：已锁屏并静音");

    /* 4 个快捷格 */
    tile(s_scr, UI_TILE_X0,                     "放歌",   0, UI_C_DIM);
    tile(s_scr, UI_TILE_X0 + UI_TILE_STEP,      "我困了", 1, UI_C_DIM);
    tile(s_scr, UI_TILE_X0 + UI_TILE_STEP * 2,  "资料",   2, UI_C_DIM);
    tile(s_scr, UI_TILE_X0 + UI_TILE_STEP * 3,  "更多",   3, UI_C_ACCENT);

    /* 大按钮：按住说话 */
    lv_obj_t *btn = box(s_scr, UI_BTN_X, UI_BTN_Y, UI_BTN_W, UI_BTN_H,
                        UI_BTN_RADIUS, UI_C_BTN, LV_OPA_COVER);
    lv_obj_set_style_border_color(btn, COL(UI_C_ACCENT), 0);
    lv_obj_set_style_border_width(btn, UI_BTN_BORDER, 0);
    label(btn, 0, 16, UI_BTN_W, UI_FONT_CN16, UI_C_ACCENT, "按住说话");

    return s_scr;
}

void ui_main_set_result(const char *text)
{
    if (!s_lbl_result) return;
    lv_label_set_text(s_lbl_result, (text && text[0]) ? text : "上次：暂无记录");
}

void ui_main_set_time(const char *time_str, const char *date_str)
{
    if (s_lbl_time && time_str) lv_label_set_text(s_lbl_time, time_str);
    if (s_lbl_date && date_str) lv_label_set_text(s_lbl_date, date_str);
}
