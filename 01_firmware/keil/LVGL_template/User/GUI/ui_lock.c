/**
 * @file    ui_lock.c
 * @brief   锁屏「层」实现（只负责画，拖拽由 ui_stage 统一处理）
 */
#include "ui_lock.h"

#include <stdint.h>

#include "ui_theme.h"

#define COL(c) lv_color_hex(c)

static lv_obj_t *s_lbl_time = NULL;
static lv_obj_t *s_lbl_date = NULL;

void ui_lock_build(lv_obj_t *parent)
{
    lv_obj_remove_style_all(parent);
    lv_obj_set_size(parent, UI_W, UI_H);
    lv_obj_set_style_bg_color(parent, COL(UI_C_BG), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* 大时钟 */
    s_lbl_time = lv_label_create(parent);
    lv_obj_set_pos(s_lbl_time, 0, UI_LOCK_TIME_Y);
    lv_obj_set_width(s_lbl_time, UI_W);
    lv_obj_set_style_text_font(s_lbl_time, UI_FONT_NUM44, 0);
    lv_obj_set_style_text_color(s_lbl_time, COL(UI_C_TEXT), 0);
    lv_obj_set_style_text_align(s_lbl_time, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_lbl_time, "--:--");   /* 真实值由 user_clock 写入 */

    /* 日期 */
    s_lbl_date = lv_label_create(parent);
    lv_obj_set_pos(s_lbl_date, 0, UI_LOCK_DATE_Y);
    lv_obj_set_width(s_lbl_date, UI_W);
    lv_obj_set_style_text_font(s_lbl_date, UI_FONT_NUM14, 0);
    lv_obj_set_style_text_color(s_lbl_date, COL(UI_C_DIM), 0);
    lv_obj_set_style_text_align(s_lbl_date, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_lbl_date, "--/--");
}

void ui_lock_set_time(const char *time_str, const char *date_str)
{
    if (s_lbl_time && time_str) lv_label_set_text(s_lbl_time, time_str);
    if (s_lbl_date && date_str) lv_label_set_text(s_lbl_date, date_str);
}
