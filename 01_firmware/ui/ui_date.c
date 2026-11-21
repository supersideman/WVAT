/**
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

#include "lvgl.h"
#include "ui_icons.h"
#include "ui_stage.h"
#include "ui_theme.h"

#define COL(c) lv_color_hex(c)

/* 页面内布局（240×280）*/
#define D_YM_Y      16      /* 年/月文字顶边 */
#define D_DAY_Y     30      /* 大号"日"顶边 */
#define D_WD_Y      94      /* 星期顶边 */
#define D_PH_X      12      /* 加号占位卡片 */
#define D_PH_Y      140
#define D_PH_W      (UI_W - 2 * D_PH_X)
#define D_PH_H      92
#define D_PH_R      14
#define D_PLUS_LEN  30      /* 加号臂长 */
#define D_PLUS_TH   6       /* 加号粗 */

static lv_obj_t *s_scr     = NULL;
static bool      s_visible = false;

static lv_obj_t *s_lbl_ym  = NULL;
static lv_obj_t *s_lbl_day = NULL;
static lv_obj_t *s_lbl_wd  = NULL;

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

    /* 未来内容的占位：一张卡片 + 一个"＋"（横竖两条圆角矩形拼出来）*/
    lv_obj_t *ph = dbox(s_scr, D_PH_X, D_PH_Y, D_PH_W, D_PH_H, D_PH_R,
                        UI_C_CARD, LV_OPA_COVER);
    dbox(ph, (D_PH_W - D_PLUS_LEN) / 2, (D_PH_H - D_PLUS_TH) / 2,
         D_PLUS_LEN, D_PLUS_TH, D_PLUS_TH / 2, UI_C_DIM, LV_OPA_60);
    dbox(ph, (D_PH_W - D_PLUS_TH) / 2, (D_PH_H - D_PLUS_LEN) / 2,
         D_PLUS_TH, D_PLUS_LEN, D_PLUS_TH / 2, UI_C_DIM, LV_OPA_60);

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
}

/* ───────────────── 对外接口 ───────────────── */

void ui_date_set_text(const char *day, const char *ym, const char *wd)
{
    if (!s_scr) return;                 /* 页面还没建过，等打开时会补 */
    if (s_lbl_day && day) lv_label_set_text(s_lbl_day, day);
    if (s_lbl_ym  && ym)  lv_label_set_text(s_lbl_ym,  ym);
    if (s_lbl_wd  && wd)  lv_label_set_text(s_lbl_wd,  wd);
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
