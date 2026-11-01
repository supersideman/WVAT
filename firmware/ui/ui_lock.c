/**
 * @file    ui_lock.c
 * @brief   锁屏页实现 · 上滑解锁
 *
 * 手势方案说明（重要）
 * --------------------
 * 不用 LVGL 的 LV_EVENT_GESTURE，因为那套依赖"速度 + 默认阈值"，
 * 而 CST816 是轮询式触摸、采样点稀疏，实机上常常判不出来。
 *
 * 这里用**更笨但更稳**的办法：
 *   PRESSED  记下按下时的 y
 *   RELEASED 比较抬起时的 y，向上位移 ≥ UI_LOCK_SWIPE_PX 就算上滑解锁
 * 再补一个"长按 1.2 秒也解锁"的兜底，避免手势失效时被永久卡在锁屏。
 */
#include "ui_lock.h"

#include <stdint.h>

#include "ui_main.h"
#include "ui_theme.h"

#define COL(c) lv_color_hex(c)

static lv_obj_t *s_lock = NULL;
static lv_obj_t *s_lbl_time = NULL;
static lv_obj_t *s_lbl_date = NULL;
static int       s_press_y = -1;      /* -1 = 当前没有按下 */

/* ───────────────── 解锁 ───────────────── */

static void do_unlock(void)
{
    s_press_y = -1;
    /* 主界面从下方推入，像"掀开"锁屏 */
    ui_main_show(LV_SCR_LOAD_ANIM_MOVE_BOTTOM);
}

/* ───────────────── 事件 ───────────────── */

static void on_pressed(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_PRESSED) return;
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    s_press_y = p.y;
}

static void on_released(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_RELEASED) return;
    if (s_press_y < 0) return;

    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) { s_press_y = -1; return; }

    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int dy = s_press_y - p.y;        /* 向上为正 */

    if (dy >= UI_LOCK_SWIPE_PX) {
        do_unlock();
    } else {
        s_press_y = -1;              /* 位移不够，不算解锁 */
    }
}

/** 兜底：长按也能解锁 */
static void on_long_pressed(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_LONG_PRESSED) return;
    do_unlock();
}

/* ───────────────── 构建 ───────────────── */

lv_obj_t *ui_lock_create(void)
{
    if (s_lock) return s_lock;

    s_lock = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_lock);
    lv_obj_set_style_bg_color(s_lock, COL(UI_C_BG), 0);
    lv_obj_set_style_bg_opa(s_lock, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_lock, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_lock, LV_OBJ_FLAG_CLICKABLE);

    /* 大时钟 */
    s_lbl_time = lv_label_create(s_lock);
    lv_obj_set_pos(s_lbl_time, 0, UI_LOCK_TIME_Y);
    lv_obj_set_width(s_lbl_time, UI_W);
    lv_obj_set_style_text_font(s_lbl_time, UI_FONT_NUM44, 0);
    lv_obj_set_style_text_color(s_lbl_time, COL(UI_C_TEXT), 0);
    lv_obj_set_style_text_align(s_lbl_time, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_lbl_time, "10:24");

    /* 日期 */
    s_lbl_date = lv_label_create(s_lock);
    lv_obj_set_pos(s_lbl_date, 0, UI_LOCK_DATE_Y);
    lv_obj_set_width(s_lbl_date, UI_W);
    lv_obj_set_style_text_font(s_lbl_date, UI_FONT_NUM14, 0);
    lv_obj_set_style_text_color(s_lbl_date, COL(UI_C_DIM), 0);
    lv_obj_set_style_text_align(s_lbl_date, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_lbl_date, "10/20");

    /* 手势：按下记 y、抬起判位移；长按兜底 */
    lv_obj_add_event_cb(s_lock, on_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_lock, on_released, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_lock, on_long_pressed, LV_EVENT_LONG_PRESSED, NULL);

    return s_lock;
}

void ui_lock_show(void)
{
    lv_obj_t *scr = ui_lock_create();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_BOTTOM, 220, 0, false);
}

void ui_lock_set_time(const char *time_str, const char *date_str)
{
    if (s_lbl_time && time_str) lv_label_set_text(s_lbl_time, time_str);
    if (s_lbl_date && date_str) lv_label_set_text(s_lbl_date, date_str);
}
