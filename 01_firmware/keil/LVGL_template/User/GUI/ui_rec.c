/**
 * @file    ui_rec.c
 * @brief   录音页实现：波形 + 录音键 + 转写文字 + 发送前确认
 */
#include "ui_rec.h"

#include <stdio.h>
#include <string.h>

#include "ui_icons.h"
#include "ui_main.h"
#include "ui_theme.h"

#define COL(c) lv_color_hex(c)

/* 中文字库（lv_conf.h 里 LV_FONT_SIMSUN_16_CJK = 1）
   含 1000 个常用汉字，够显示日常语音指令 */
LV_FONT_DECLARE(lv_font_simsun_16_cjk);

typedef enum {
    REC_IDLE = 0,      /* 待机：等待按下录音键 */
    REC_RECORDING,     /* 录音中：波形跳动 */
    REC_WAITING,       /* 已停止，等待识别结果 */
    REC_TEXT,          /* 已识别：显示文字，可确认或取消 */
} rec_state_t;

static lv_obj_t *s_scr = NULL;
static lv_obj_t *s_prev_scr = NULL;
static lv_obj_t *s_wave[UI_REC_WAVE_BARS];
static lv_obj_t *s_lbl_text = NULL;
static lv_obj_t *s_btn_mic = NULL;
static lv_obj_t *s_grp_confirm = NULL;   /* 确认/取消两个按钮的容器 */

static rec_state_t s_state = REC_IDLE;
static bool        s_visible = false;
static uint32_t    s_tick = 0;
static uint32_t    s_rand = 12345;

static char s_text[96] = {0};            /* 当前转写文字 */
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
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

/** 波形高度：用线性同余发生器做"伪随机"，看起来像真实音量起伏 */
static int next_height(void)
{
    s_rand = s_rand * 1103515245u + 12345u;
    int span = UI_REC_WAVE_HMAX - UI_REC_WAVE_HMIN;
    return UI_REC_WAVE_HMIN + (int)((s_rand >> 16) % (uint32_t)(span + 1));
}

static void set_bars(int h)
{
    for (int i = 0; i < UI_REC_WAVE_BARS; i++) {
        lv_obj_set_height(s_wave[i], h);
        lv_obj_set_y(s_wave[i], UI_REC_WAVE_CY - h / 2);
    }
}

/* ───────────────── 画面刷新 ───────────────── */

static void refresh(void)
{
    switch (s_state) {
    case REC_IDLE:
        set_bars(UI_REC_WAVE_HMIN);
        lv_obj_set_style_bg_color(s_btn_mic, COL(UI_C_BTN), 0);
        lv_obj_set_style_border_color(s_btn_mic, COL(UI_C_ACCENT), 0);
        lv_label_set_text(s_lbl_text, "");
        lv_obj_add_flag(s_grp_confirm, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_btn_mic, LV_OBJ_FLAG_HIDDEN);
        break;

    case REC_RECORDING:
        lv_obj_set_style_bg_color(s_btn_mic, COL(UI_C_DANGER), 0);
        lv_obj_set_style_border_color(s_btn_mic, COL(UI_C_DANGER), 0);
        lv_label_set_text(s_lbl_text, "");
        lv_obj_add_flag(s_grp_confirm, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_btn_mic, LV_OBJ_FLAG_HIDDEN);
        break;

    case REC_WAITING:
        set_bars(UI_REC_WAVE_HMIN);
        lv_label_set_text(s_lbl_text, "......");
        lv_obj_add_flag(s_grp_confirm, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_btn_mic, LV_OBJ_FLAG_HIDDEN);
        break;

    case REC_TEXT:
        set_bars(UI_REC_WAVE_HMIN);
        lv_label_set_text(s_lbl_text, s_text);
        lv_obj_add_flag(s_btn_mic, LV_OBJ_FLAG_HIDDEN);      /* 让位给确认键 */
        lv_obj_clear_flag(s_grp_confirm, LV_OBJ_FLAG_HIDDEN);
        break;
    }
}

/* ───────────────── 事件 ───────────────── */

static void on_mic_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    if (s_state == REC_IDLE) {
        /* 开始"录音" */
        s_state = REC_RECORDING;
        s_tick  = lv_tick_get();
        refresh();

        /* 请求识别：复用 A08 这条指令（真接麦后这里改成开始采集 + 流式上行）*/
        ui_intent_cb_t cb = ui_main_get_intent_cb();
        if (cb) cb(UI_ICON_MIC);
        return;
    }

    if (s_state == REC_RECORDING) {
        /* 停止录音，等识别结果 */
        s_state = REC_WAITING;
        s_tick  = lv_tick_get();
        refresh();
        return;
    }
}

static void on_cancel_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    /* 丢弃这次识别结果，回到待机重新录 */
    s_text[0] = '\0';
    s_state = REC_IDLE;
    refresh();
}

static void on_ok_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (s_text[0] == '\0') return;

    /* 把确认过的文字交给链路层，走 T 慢通道（网关用关键词/LLM 理解并执行） */
    ui_text_cb_t cb = ui_main_get_text_cb();
    if (cb) cb(s_text);

    ui_rec_close();
}

static void on_back_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_rec_close();
}

/* ───────────────── 定时器 ───────────────── */

static void rec_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_visible) return;

    if (s_state == REC_RECORDING) {
        /* 波形跳动：每 100ms 刷新一次高度 */
        for (int i = 0; i < UI_REC_WAVE_BARS; i++) {
            int h = next_height();
            lv_obj_set_height(s_wave[i], h);
            lv_obj_set_y(s_wave[i], UI_REC_WAVE_CY - h / 2);
        }
    } else if (s_state == REC_WAITING) {
        /* 等太久没结果 → 视为识别失败，直接回待机（真接麦后可换成提示）*/
        if (lv_tick_elaps(s_tick) > UI_ST_REC_MAX_MS) {
            s_state = REC_IDLE;
            refresh();
        }
    }
}

/* ───────────────── 构建 ───────────────── */

static void build_screen(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_scr);
    lv_obj_set_size(s_scr, UI_W, UI_H);
    lv_obj_set_style_bg_color(s_scr, COL(UI_C_BG), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* 顶部条：左上角返回 */
    mk_box(s_scr, 0, 0, UI_W, UI_REC_TOP_H, 0, UI_C_CARD, LV_OPA_COVER);
    lv_obj_t *back = mk_box(s_scr, 2, 1, UI_REC_BACK_SIZE, UI_REC_BACK_SIZE, 8,
                            UI_C_CARD, LV_OPA_COVER);
    ui_icon_draw(back, UI_ICON_BACK, UI_REC_BACK_SIZE / 2, UI_REC_BACK_SIZE / 2,
                 UI_REC_BACK_SIZE - 2, UI_C_TEXT);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(back, COL(UI_C_PRESSED), LV_STATE_PRESSED);
    lv_obj_add_event_cb(back, on_back_clicked, LV_EVENT_CLICKED, NULL);

    /* 波形 */
    int total = (UI_REC_WAVE_BARS - 1) * UI_REC_WAVE_GAP;
    int x0 = UI_W / 2 - total / 2;
    for (int i = 0; i < UI_REC_WAVE_BARS; i++) {
        s_wave[i] = mk_box(s_scr,
                           x0 + i * UI_REC_WAVE_GAP - UI_REC_WAVE_W / 2,
                           UI_REC_WAVE_CY - UI_REC_WAVE_HMIN / 2,
                           UI_REC_WAVE_W, UI_REC_WAVE_HMIN,
                           UI_REC_WAVE_W / 2, UI_C_ACCENT, LV_OPA_COVER);
    }

    /* 转写文字区 */
    s_lbl_text = lv_label_create(s_scr);
    lv_obj_set_pos(s_lbl_text, UI_REC_TEXT_X, UI_REC_TEXT_Y);
    lv_obj_set_size(s_lbl_text, UI_REC_TEXT_W, UI_REC_TEXT_H);
    lv_obj_set_style_text_font(s_lbl_text, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_style_text_color(s_lbl_text, COL(UI_C_TEXT), 0);
    lv_obj_set_style_text_align(s_lbl_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_lbl_text, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_lbl_text, "");

    /* 录音大圆键（居中） */
    s_btn_mic = mk_box(s_scr, UI_W / 2 - UI_REC_MIC_SIZE / 2, UI_REC_BTN_Y,
                       UI_REC_MIC_SIZE, UI_REC_MIC_SIZE, LV_RADIUS_CIRCLE,
                       UI_C_BTN, LV_OPA_COVER);
    lv_obj_set_style_border_width(s_btn_mic, 2, 0);
    lv_obj_set_style_border_color(s_btn_mic, COL(UI_C_ACCENT), 0);
    ui_icon_draw(s_btn_mic, UI_ICON_MIC, UI_REC_MIC_SIZE / 2, UI_REC_MIC_SIZE / 2, 28,
                 UI_C_ACCENT);
    lv_obj_add_flag(s_btn_mic, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_btn_mic, COL(UI_C_PRESSED), LV_STATE_PRESSED);
    lv_obj_add_event_cb(s_btn_mic, on_mic_clicked, LV_EVENT_CLICKED, NULL);

    /* 确认/取消：两个圆角按钮，和录音键占同一块区域（互斥显示）*/
    s_grp_confirm = mk_box(s_scr, 0, UI_REC_BTN_Y, UI_W, UI_REC_BTN_H, 0,
                           UI_C_BG, LV_OPA_TRANSP);
    lv_obj_add_flag(s_grp_confirm, LV_OBJ_FLAG_HIDDEN);

    int total_w = UI_REC_OK_W * 2 + UI_REC_OK_GAP;
    int left_x = UI_W / 2 - total_w / 2;

    lv_obj_t *cancel = mk_box(s_grp_confirm, left_x, 0, UI_REC_OK_W, UI_REC_BTN_H, 14,
                              UI_C_CARD, LV_OPA_COVER);
    ui_icon_draw(cancel, UI_ICON_CROSS, UI_REC_OK_W / 2, UI_REC_BTN_H / 2, 26, UI_C_DANGER);
    lv_obj_add_flag(cancel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(cancel, COL(UI_C_PRESSED), LV_STATE_PRESSED);
    lv_obj_add_event_cb(cancel, on_cancel_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ok = mk_box(s_grp_confirm, left_x + UI_REC_OK_W + UI_REC_OK_GAP, 0,
                          UI_REC_OK_W, UI_REC_BTN_H, 14, UI_C_CARD, LV_OPA_COVER);
    lv_obj_set_style_border_width(ok, 2, 0);
    lv_obj_set_style_border_color(ok, COL(UI_C_ACCENT), 0);
    ui_icon_draw(ok, UI_ICON_OK, UI_REC_OK_W / 2, UI_REC_BTN_H / 2, 26, UI_C_ACCENT);
    lv_obj_add_flag(ok, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(ok, COL(UI_C_PRESSED), LV_STATE_PRESSED);
    lv_obj_add_event_cb(ok, on_ok_clicked, LV_EVENT_CLICKED, NULL);

    s_timer = lv_timer_create(rec_timer_cb, 100, NULL);
}

/* ───────────────── 对外接口 ───────────────── */

void ui_rec_open(void)
{
    if (!s_scr) build_screen();

    if (!s_visible) {
        s_prev_scr = lv_scr_act();
        s_state = REC_IDLE;
        s_text[0] = '\0';
        refresh();
        lv_scr_load_anim(s_scr, LV_SCR_LOAD_ANIM_FADE_ON, 120, 0, false);
        s_visible = true;
    }
}

void ui_rec_close(void)
{
    if (!s_visible) return;

    s_visible = false;
    lv_obj_t *back = s_prev_scr ? s_prev_scr : lv_scr_act();
    s_prev_scr = NULL;
    if (back && back != s_scr) {
        lv_scr_load_anim(back, LV_SCR_LOAD_ANIM_FADE_ON, 120, 0, false);
    }
}

void ui_rec_set_text(const char *text)
{
    if (!s_scr || !text) return;

    /* 只保留一行内能放下的量，防止撑爆缓冲区 */
    strncpy(s_text, text, sizeof(s_text) - 1);
    s_text[sizeof(s_text) - 1] = '\0';

    s_state = REC_TEXT;
    refresh();
}

bool ui_rec_visible(void)
{
    return s_visible;
}
