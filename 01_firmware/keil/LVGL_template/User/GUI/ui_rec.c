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

/* 中文字库：由 05_scripts/gen_cn_font.py 从系统字体生成（14px，GB2312 一级汉字 3755 字）
   不用 LVGL 自带的 simsun_16_cjk —— 它只有 1000 个常用偏旁，实测缺"帮开资现调"等常用字 */
LV_FONT_DECLARE(lv_font_cn_14);

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
static lv_obj_t *s_lbl_timer = NULL;     /* 顶部右侧：已录时长 */
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
    /* 计时标签只在录音态显示 */
    if (s_lbl_timer) {
        if (s_state == REC_RECORDING) lv_obj_clear_flag(s_lbl_timer, LV_OBJ_FLAG_HIDDEN);
        else                          lv_obj_add_flag(s_lbl_timer, LV_OBJ_FLAG_HIDDEN);
    }

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

/** 开始录音：记时间戳、切状态；真接麦后这里还要"启动 I2S 采集 + 开始流式上行" */
static void start_recording(void)
{
    s_state = REC_RECORDING;
    s_tick  = lv_tick_get();
    s_text[0] = '\0';
    refresh();
}

/** 结束录音：太短视为误触直接丢弃；否则进入等待识别，并发出识别请求 */
static void stop_recording(void)
{
    uint32_t dur = lv_tick_elaps(s_tick);

    if (dur < UI_REC_MIN_MS) {
        /* 误触：不发给网关，直接回待机 */
        s_state = REC_IDLE;
        refresh();
        return;
    }

    s_state = REC_WAITING;
    s_tick  = lv_tick_get();
    refresh();

    /* 请求识别：复用 A08（真接麦后改成"结束上行 + 请求转写"）*/
    ui_intent_cb_t cb = ui_main_get_intent_cb();
    if (cb) cb(UI_ICON_MIC);
}

static void on_mic_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    if (s_state == REC_IDLE) {
        start_recording();
    } else if (s_state == REC_RECORDING) {
        stop_recording();
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
        uint32_t dur = lv_tick_elaps(s_tick);

        /* 到上限自动停止：不中断用户，但也不能无限录 */
        if (dur >= UI_REC_MAX_MS) {
            stop_recording();
            return;
        }

        /* 波形跳动：每 100ms 刷新一次高度 */
        for (int i = 0; i < UI_REC_WAVE_BARS; i++) {
            int h = next_height();
            lv_obj_set_height(s_wave[i], h);
            lv_obj_set_y(s_wave[i], UI_REC_WAVE_CY - h / 2);
        }

        /* 顶部显示已录时长，例如 "3.2s"（同时让用户知道快到 10 秒上限）*/
        if (s_lbl_timer) {
            lv_label_set_text_fmt(s_lbl_timer, "%u.%us",
                                  (unsigned)(dur / 1000u), (unsigned)((dur % 1000u) / 100u));
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

    /* 已录时长（仅录音时显示，让用户知道快到 10 秒上限）*/
    s_lbl_timer = lv_label_create(s_scr);
    lv_obj_set_pos(s_lbl_timer, UI_REC_TIMER_X, UI_REC_TIMER_Y);
    lv_obj_set_style_text_font(s_lbl_timer, &lv_font_cn_14, 0);
    lv_obj_set_style_text_color(s_lbl_timer, COL(UI_C_WARN), 0);
    lv_label_set_text(s_lbl_timer, "");
    lv_obj_add_flag(s_lbl_timer, LV_OBJ_FLAG_HIDDEN);

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
    lv_obj_set_style_text_font(s_lbl_text, &lv_font_cn_14, 0);
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

    /* 两层截断：先按界面能显示的字数，再按缓冲区大小。
       文字区约 4 行 × 15 字 ≈ 60 汉字；协议上限更严（60 字节 ≈ 20 汉字），
       正常不会触发，但闸门必须有——否则长句会溢出到按钮区。 */
    size_t n = 0;
    while (text[n] != '\0' && n < UI_REC_TEXT_MAX && n < sizeof(s_text) - 1) {
        n++;
    }
    memcpy(s_text, text, n);
    s_text[n] = '\0';

    s_state = REC_TEXT;
    refresh();
}

bool ui_rec_visible(void)
{
    return s_visible;
}
