/**
 * @file    ui_status.c
 * @brief   状态页实现：录音中（红波形）/ 成功（青勾）/ 失败（红叉）
 */
#include "ui_status.h"

#include <stdint.h>

#include "ui_theme.h"

#define COL(c) lv_color_hex(c)

static lv_obj_t *s_scr      = NULL;   /* 状态页屏幕 */
static lv_obj_t *s_body     = NULL;   /* 每次显示时重建内容的容器 */
static lv_obj_t *s_prev_scr = NULL;   /* 进入前的界面，用于返回 */

static bool     s_visible  = false;
static uint32_t s_tick     = 0;       /* 进入当前状态的时间戳 */
static uint32_t s_hold_ms  = 0;       /* 0 = 一直停留（录音中）；>0 = 到点自动返回 */
static lv_timer_t *s_timer = NULL;

/* ───────────────── 工具 ───────────────── */

static lv_obj_t *mk_box(lv_obj_t *parent, int x, int y, int w, int h,
                        int radius, uint32_t color, lv_opa_t opa)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, COL(color), 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_set_style_radius(o, (radius == LV_RADIUS_CIRCLE) ? LV_RADIUS_CIRCLE : (lv_coord_t)radius, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

/* ───────────────── 三种画面的绘制 ───────────────── */

/** 录音中：顶部红点 + 一排高低起伏的红色波形条 */
static void draw_recording(void)
{
    lv_obj_t *body = mk_box(s_scr, 0, 0, UI_W, UI_H, 0, UI_C_BG, LV_OPA_COVER);
    s_body = body;

    /* 顶部红点（呼吸感留给以后，这里先静态） */
    mk_box(body, UI_W / 2 - UI_ST_DOT_R, UI_ST_DOT_Y - UI_ST_DOT_R,
           UI_ST_DOT_R * 2, UI_ST_DOT_R * 2, LV_RADIUS_CIRCLE,
           UI_C_DANGER, LV_OPA_COVER);

    /* 波形：中间高、两端低，用固定的"鼓包"形状，不需要随机数 */
    int total = (UI_ST_BARS - 1) * UI_ST_BAR_GAP;
    int x0 = UI_W / 2 - total / 2;
    for (int i = 0; i < UI_ST_BARS; i++) {
        int d = i - UI_ST_BARS / 2;
        if (d < 0) d = -d;                       /* 到中心的距离 */
        int h = UI_ST_BAR_MAX_H - d * (UI_ST_BAR_MAX_H - UI_ST_BAR_MIN_H) / (UI_ST_BARS / 2);
        if (h < UI_ST_BAR_MIN_H) h = UI_ST_BAR_MIN_H;
        int x = x0 + i * UI_ST_BAR_GAP - UI_ST_BAR_W / 2;
        mk_box(body, x, UI_ST_CY - h / 2, UI_ST_BAR_W, h, UI_ST_BAR_W / 2,
               UI_C_DANGER, LV_OPA_COVER);
    }
}

/** 结果：外圈 + 大图标（青勾 / 红叉） */
static void draw_result(ui_icon_t kind, bool ok)
{
    (void)kind;
    lv_obj_t *body = mk_box(s_scr, 0, 0, UI_W, UI_H, 0, UI_C_BG, LV_OPA_COVER);
    s_body = body;

    uint32_t color = ok ? UI_C_ACCENT : UI_C_DANGER;
    int r = UI_ST_RING_R;

    /* 外圈：一个透明填充、只留描边的圆 */
    lv_obj_t *ring = mk_box(body, UI_W / 2 - r, UI_ST_CY - r, r * 2, r * 2,
                            LV_RADIUS_CIRCLE, UI_C_BG, LV_OPA_TRANSP);
    lv_obj_set_style_border_color(ring, COL(color), 0);
    lv_obj_set_style_border_width(ring, UI_ST_RING_W, 0);

    ui_icon_draw(body, ok ? UI_ICON_OK : UI_ICON_CROSS,
                 UI_W / 2, UI_ST_CY, UI_ST_ICON_SZ, color);
}

/* ───────────────── 内部：显示/隐藏 ───────────────── */

static void clear_body(void)
{
    if (s_body) {
        lv_obj_del(s_body);
        s_body = NULL;
    }
}

/** 常驻定时器：负责"到点自动返回"和"录音状态超时" */
static void status_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_visible) return;

    uint32_t elapsed = lv_tick_elaps(s_tick);
    if (s_hold_ms > 0) {
        if (elapsed >= s_hold_ms) ui_status_hide();
        return;
    }

    /* 录音状态：长时间没有回执也要给个结局，否则用户以为卡死 */
    if (elapsed >= UI_ST_REC_MAX_MS) {
        draw_result(UI_ICON_MIC, false);
        s_hold_ms = UI_ST_HOLD_MS;
        s_tick    = lv_tick_get();
    }
}

static void ensure_screen(void)
{
    if (s_scr) return;

    s_scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_scr);
    lv_obj_set_size(s_scr, UI_W, UI_H);
    lv_obj_set_style_bg_color(s_scr, COL(UI_C_BG), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    s_timer = lv_timer_create(status_timer_cb, 100, NULL);
}

/** 进入状态页（第一次进入时记住来源界面） */
static void enter(void)
{
    ensure_screen();
    if (!s_visible) {
        s_prev_scr = lv_scr_act();          /* 记住从哪个界面来的 */
        lv_scr_load_anim(s_scr, LV_SCR_LOAD_ANIM_FADE_ON, 120, 0, false);
        s_visible = true;
    }
    s_tick = lv_tick_get();
}

/* ───────────────── 对外接口 ───────────────── */

void ui_status_recording(void)
{
    enter();
    clear_body();
    draw_recording();
    s_hold_ms = 0;                          /* 一直停留，等结果或超时 */
}

void ui_status_show_result(ui_icon_t kind, bool ok, uint32_t ms)
{
    enter();
    clear_body();
    draw_result(kind, ok);
    s_hold_ms = (ms == 0) ? UI_ST_HOLD_MS : ms;
    s_tick    = lv_tick_get();
}

void ui_status_hide(void)
{
    if (!s_visible) return;

    s_visible = false;
    s_hold_ms = 0;

    lv_obj_t *back = s_prev_scr ? s_prev_scr : lv_scr_act();
    s_prev_scr = NULL;

    if (back && back != s_scr) {
        lv_scr_load_anim(back, LV_SCR_LOAD_ANIM_FADE_ON, 120, 0, false);
    }
}

bool ui_status_visible(void)
{
    return s_visible;
}
