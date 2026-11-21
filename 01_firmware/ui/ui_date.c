﻿/**
 * @file    ui_date.c
 * @brief   日期页 · LVGL 8.2
 *
 * 三页横向导航里最左边那页：
 *
 *      ↑ 年/月        "2026/11"
 *        日（大号）    "21"
 *        星期          "FRI"
 *        ┌──────────┐
 *        │    ＋    │   ← 以后要加的内容先占个位
 *        └──────────┘
 *        ○ ● ○    >
 *
 * 入口：主界面整层背景向右滑；出口：向左滑 / 点底部条。
 * 界面依旧不放中文，全部数字与英文缩写，零字库成本。
 */
#include "ui_date.h"

#include <stdio.h>

#include "lvgl.h"
#include "ui_icons.h"
#include "ui_stage.h"
#include "ui_theme.h"

#define COL(c) lv_color_hex(c)

/* 页面内布局（240×280）*/
#define D_YM_Y      16      /* 年/月文字顶边 */
#define D_DAY_Y     30      /* 大号"日"顶边 */
#define D_WD_Y      94      /* 星期顶边 */
#define D_PH_X      12      /* 天气/占位卡片 */
#define D_PH_Y      140
#define D_PH_W      (UI_W - 2 * D_PH_X)
#define D_PH_H      92
#define D_PH_R      14
#define WX_ICON_SZ  48      /* 天气图标绘制区边长 */

static lv_obj_t *s_scr     = NULL;
static bool      s_visible = false;

static lv_obj_t *s_lbl_ym  = NULL;
static lv_obj_t *s_lbl_day = NULL;
static lv_obj_t *s_lbl_wd  = NULL;

/* 天气：卡片里左边图标、右边温度；没数据时退回"＋"占位 */
static lv_obj_t *s_ph_card  = NULL;   /* 卡片本体 */
static lv_obj_t *s_wx_none  = NULL;   /* 没有数据时的淡色 "--" 提示 */
static lv_obj_t *s_wx_icon  = NULL;   /* 图标容器（每次清空重画）*/
static lv_obj_t *s_wx_temp  = NULL;   /* 温度数字 */
static lv_obj_t *s_wx_deg   = NULL;   /* 温度右上角的小圆圈（℃ 的度）*/
static int       s_wx_kind  = -1;     /* -1 = 还没有数据 */
static int       s_wx_temp_c = 0;

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

static lv_obj_t *dlabel(lv_obj_t *parent, int y, int h, const lv_font_t *font,
                        uint32_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, 0, y);
    lv_obj_set_width(l, UI_W);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, COL(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, txt);
    (void)h;
    return l;
}

/* ───────────────── 天气图标（纯图元，零位图）───────────────── */
/*
 * 0=晴   圆 + 四向光线
 * 1=多云 两个重叠圆（云团）
 * 2=雨   云团 + 三条短竖线
 * 3=雪   云团 + 三个点
 * 只画四种最常用的形态：雷雨归到雨、雾归到多云（网关侧已经归一）。
 */
static void draw_wx_icon(lv_obj_t *parent, int kind, uint32_t color)
{
    const int S  = WX_ICON_SZ;
    const int cx = S / 2;
    const int cy = S / 2;
    const lv_opa_t op = LV_OPA_COVER;

    switch (kind) {
    case 0:                                            /* 晴 */
        dbox(parent, cx - 9, cy - 9, 18, 18, 9, color, op);
        dbox(parent, cx - 2, 1,          4, 8, 2, color, op);
        dbox(parent, cx - 2, S - 9,      4, 8, 2, color, op);
        dbox(parent, 1,      cy - 2,     8, 4, 2, color, op);
        dbox(parent, S - 9,  cy - 2,     8, 4, 2, color, op);
        break;

    case 1:                                            /* 多云 */
        dbox(parent, 5,  cy - 3, 21, 21, 10, color, op);
        dbox(parent, 18, cy - 13, 17, 17, 8, color, op);
        break;

    case 2:                                            /* 雨 */
        dbox(parent, 4,  cy - 13, 20, 20, 10, color, op);
        dbox(parent, 16, cy - 17, 16, 16, 8, color, op);
        for (int i = 0; i < 3; i++) {
            dbox(parent, 8 + i * 8, cy + 13, 3, 9, 1, color, op);
        }
        break;

    case 3:                                            /* 雪 */
        dbox(parent, 4,  cy - 13, 20, 20, 10, color, op);
        dbox(parent, 16, cy - 17, 16, 16, 8, color, op);
        for (int i = 0; i < 3; i++) {
            dbox(parent, 9 + i * 8, cy + 14, 4, 4, 2, color, op);
        }
        break;

    default:
        break;
    }
}

/** 把天气显示刷新到卡片上：有数据就画图标+温度，没数据就显示"＋" */
static void repaint_weather(void)
{
    if (!s_ph_card) return;

    bool has = (s_wx_kind >= 0);
    if (s_wx_none) { if (has) lv_obj_add_flag(s_wx_none, LV_OBJ_FLAG_HIDDEN);
                     else     lv_obj_clear_flag(s_wx_none, LV_OBJ_FLAG_HIDDEN); }

    if (s_wx_icon) {
        lv_obj_clean(s_wx_icon);                       /* 先清掉旧图标的所有图元 */
        if (has) draw_wx_icon(s_wx_icon, s_wx_kind, UI_C_INFO);
        if (has) lv_obj_clear_flag(s_wx_icon, LV_OBJ_FLAG_HIDDEN);
        else     lv_obj_add_flag(s_wx_icon, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_wx_temp) {
        if (has) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%d", s_wx_temp_c);
            lv_label_set_text(s_wx_temp, buf);
            lv_obj_clear_flag(s_wx_temp, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(s_wx_temp, "");
            lv_obj_add_flag(s_wx_temp, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_wx_deg) {
        if (has) lv_obj_clear_flag(s_wx_deg, LV_OBJ_FLAG_HIDDEN);
        else     lv_obj_add_flag(s_wx_deg, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ───────────────── 手势 ───────────────── */

/** 页面底部条 / 背景向左滑 → 回主界面（主界面在本页右边）*/
static void on_back(lv_event_t *e)
{
    (void)e;
    ui_date_close();
}

static int s_press_x = -1;

static void on_bg_pressed(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_PRESSED) return;
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    s_press_x = p.x;
}

static void on_bg_released(lv_event_t *e)
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

    if (dx <= -UI_PAGE_SWIPE_PX) {      /* 向左滑 → 右页：主界面 */
        ui_date_close();
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

    s_lbl_ym  = dlabel(s_scr, D_YM_Y,  18, UI_FONT_NUM14, UI_C_DIM,    "----/--");
    s_lbl_day = dlabel(s_scr, D_DAY_Y, 56, UI_FONT_NUM44, UI_C_TEXT,   "--");
    s_lbl_wd  = dlabel(s_scr, D_WD_Y,  18, UI_FONT_NUM14, UI_C_ACCENT, "---");

    /* 天气卡片：有数据时"图标 + 温度"，没有数据时显示一个淡淡的"＋" */
    s_ph_card = dbox(s_scr, D_PH_X, D_PH_Y, D_PH_W, D_PH_H, D_PH_R,
                     UI_C_CARD, LV_OPA_COVER);

    /* 没有天气数据时：卡片中央一个淡色 "--"（和时钟的 "--:--" 一个风格）*/
    s_wx_none = lv_label_create(s_ph_card);
    lv_obj_set_pos(s_wx_none, 0, (D_PH_H - 20) / 2);
    lv_obj_set_width(s_wx_none, D_PH_W);
    lv_obj_set_style_text_font(s_wx_none, UI_FONT_NUM14, 0);
    lv_obj_set_style_text_color(s_wx_none, COL(UI_C_DIM), 0);
    lv_obj_set_style_text_align(s_wx_none, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_wx_none, "--");

    /* 图标容器：左半侧，垂直居中 */
    s_wx_icon = dbox(s_ph_card, 26, (D_PH_H - WX_ICON_SZ) / 2,
                     WX_ICON_SZ, WX_ICON_SZ, 0, UI_C_CARD, LV_OPA_TRANSP);

    /* 温度：右半侧，44 号数字右对齐；右上角一个小圆当"°" */
    s_wx_temp = lv_label_create(s_ph_card);
    lv_obj_set_pos(s_wx_temp, 88, 6);
    lv_obj_set_size(s_wx_temp, 92, 56);
    lv_obj_set_style_text_font(s_wx_temp, UI_FONT_NUM44, 0);
    lv_obj_set_style_text_color(s_wx_temp, COL(UI_C_TEXT), 0);
    lv_obj_set_style_text_align(s_wx_temp, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(s_wx_temp, "");

    s_wx_deg = dbox(s_ph_card, 186, 16, 7, 7, 3, UI_C_TEXT, LV_OPA_COVER);

    /* 底部页面条：三个圆点（本页在最左）+ 右侧 ">" */
    lv_obj_t *bar = dbox(s_scr, 0, UI_PAGEBAR_Y, UI_W, UI_PAGEBAR_H, 0,
                         UI_C_BG, LV_OPA_TRANSP);
    ui_page_dots(bar, UI_PAGEBAR_H, 0);
    lv_obj_t *rg = dbox(bar, UI_W - 26, (UI_PAGEBAR_H - 16) / 2, 16, 16, 0,
                        UI_C_BG, LV_OPA_TRANSP);
    lv_obj_t *rgl = lv_label_create(rg);
    lv_obj_set_style_text_font(rgl, UI_FONT_NUM14, 0);
    lv_obj_set_style_text_color(rgl, COL(UI_C_DIM), 0);
    lv_label_set_text(rgl, ">");

    lv_obj_add_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bar, on_back, LV_EVENT_CLICKED, NULL);

    /* 整层背景：向左滑回主界面（放最后，只兜住没被子控件接走的触点）*/
    lv_obj_add_flag(s_scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_scr, on_bg_pressed,  LV_EVENT_PRESSED,  NULL);
    lv_obj_add_event_cb(s_scr, on_bg_released, LV_EVENT_RELEASED, NULL);

    /* 建完立刻按当前状态画一次（若天气先到、页面后建，也能正确显示）*/
    repaint_weather();
}

/* ───────────────── 对外接口 ───────────────── */

void ui_date_set_text(const char *day, const char *ym, const char *wd)
{
    if (!s_scr) return;                 /* 页面还没建过，等打开时会补 */
    if (s_lbl_day && day) lv_label_set_text(s_lbl_day, day);
    if (s_lbl_ym  && ym)  lv_label_set_text(s_lbl_ym,  ym);
    if (s_lbl_wd  && wd)  lv_label_set_text(s_lbl_wd,  wd);
}

void ui_date_set_weather(int kind, int temp)
{
    s_wx_kind   = kind;
    s_wx_temp_c = temp;
    repaint_weather();
}

void ui_date_open(void)
{
    if (!s_scr) build();
    lv_scr_load_anim(s_scr, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
    s_visible = true;
}

void ui_date_close(void)
{
    if (!s_visible) return;
    s_visible = false;
    ui_stage_show_home(LV_SCR_LOAD_ANIM_MOVE_LEFT);
}

bool ui_date_visible(void)
{
    return s_visible;
}
