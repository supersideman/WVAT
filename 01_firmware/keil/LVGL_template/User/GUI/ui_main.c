/**
 * @file    ui_main.c
 * @brief   主界面（图形化版）· LVGL 8.2
 *
 * 布局：时钟(数字) + 日期(数字) + 结果回执(图标) + 3 个意图格(图标) + 麦克风大按钮
 * 全屏没有中文，只用到内置 Montserrat 数字 → 零字库成本
 */
#include "ui_main.h"
#include "ui_theme.h"
#include "ui_icons.h"
#include "ui_drawer.h"
#include "ui_status.h"
#include "ui_rec.h"
#include "ui_history.h"

#define COL(c) lv_color_hex(c)

LV_FONT_DECLARE(lv_font_cn_14);   /* 回执文字用中文字库 */

static lv_obj_t *s_scr;
static lv_obj_t *s_lbl_time;
static lv_obj_t *s_lbl_date;
static lv_obj_t *s_lbl_reply;      /* 回执横条中间的文字 */

/* 结果回执：左侧意图图标 + 右侧状态图标 */
#define RESULT_ICON_SZ 20
static ui_icon_t s_res_intent = UI_ICON_MUSIC;
static uint32_t   s_res_intent_col = UI_C_DIM;
static bool       s_res_ok = true;

static lv_obj_t *s_res_slot_intent;   /* 左侧图标容器 */
static lv_obj_t *s_res_slot_status;   /* 右侧状态容器 */

/* 链路状态指示（右上角：圆点 + 字母）*/
static lv_obj_t *s_link_dot;
static lv_obj_t *s_link_txt;
static ui_link_t s_link_shown = UI_LINK_NONE;

static ui_intent_cb_t s_intent_cb = NULL;   /* 链路层注册的意图回调（可为空）*/
static ui_text_cb_t   s_text_cb   = NULL;   /* 录音页确认发送时用 */
static ui_mic_cb_t    s_mic_cb    = NULL;   /* 录音开始/结束（电脑麦克风兜底）*/

/* ───────────────── 工具 ───────────────── */

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, int radius,
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

static lv_obj_t *num_label(lv_obj_t *parent, int x, int y, int w,
                           const lv_font_t *font, uint32_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, COL(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, txt);
    return l;
}

/* ───────────────── 触摸事件 ───────────────── */

/* 前向声明：回调里要用 */
static void on_tile_clicked(lv_event_t *e);

/**
 * 通用：让一个对象"按下去有反应"
 * - 显式加 CLICKABLE（保险，base lv_obj 默认就有）
 * - PRESSED 状态换背景色 → 手指按下立刻能看到
 */
static void make_pressable(lv_obj_t *o, lv_event_cb_t cb, void *user_data)
{
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(o, COL(UI_C_PRESSED), LV_STATE_PRESSED);
    if (cb) lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, user_data);
}

/** 意图格被点击：如果有链路就交给它，否则本地演示 */
static void on_tile_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_icon_t kind = (ui_icon_t)(intptr_t)lv_event_get_user_data(e);
    if (s_intent_cb) {
        s_intent_cb(kind);      /* 真机：发指令，等回执再更新回执条 */
    } else {
        ui_main_set_result(kind, true);   /* 无链路：本地假回执，便于调试 UI */
    }
}

/** 点「更多」格：滑入抽屉页（不发送意图） */
static void on_more_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_drawer_open();
}

/** 麦克风大按钮被点击 */
static void on_receipt_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_history_open();     /* 回执条 → 语音历史 */
}

static void on_talk_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    /* 语音是"有过程"的操作：进入专门的录音页（波形 + 转写 + 确认）*/
    ui_rec_open();
}

/** 一个意图格：66×66 圆角卡片 + 居中图标（可点、有按下反馈） */
static lv_obj_t *tile(lv_obj_t *parent, int x, ui_icon_t kind, uint32_t color)
{
    lv_obj_t *card = box(parent, x, UI_TILE_Y, UI_TILE_SIZE, UI_TILE_SIZE,
                         UI_TILE_RADIUS, UI_C_CARD, LV_OPA_COVER);
    ui_icon_draw(card, kind, UI_TILE_SIZE / 2, UI_ICON_CY, 30, color);
    make_pressable(card, on_tile_clicked, (void *)(intptr_t)kind);
    return card;
}

/** 「更多」格：外形一样，但点它是跳转而不是发意图 */
static lv_obj_t *tile_more(lv_obj_t *parent, int x)
{
    lv_obj_t *card = box(parent, x, UI_TILE_Y, UI_TILE_SIZE, UI_TILE_SIZE,
                         UI_TILE_RADIUS, UI_C_CARD, LV_OPA_COVER);
    ui_icon_draw(card, UI_ICON_GRID, UI_TILE_SIZE / 2, UI_ICON_CY, 30, UI_C_ACCENT);
    make_pressable(card, on_more_clicked, NULL);
    return card;
}

/** 重画结果回执（清空容器再画，只有 2 个对象，代价可接受） */
static void result_repaint(void)
{
    if (!s_res_slot_intent) return;
    lv_obj_clean(s_res_slot_intent);
    lv_obj_clean(s_res_slot_status);
    ui_icon_draw(s_res_slot_intent, s_res_intent, RESULT_ICON_SZ / 2 + 1,
                 RESULT_ICON_SZ / 2, RESULT_ICON_SZ, s_res_intent_col);
    ui_icon_draw(s_res_slot_status,
                 s_res_ok ? UI_ICON_OK : UI_ICON_CROSS,
                 RESULT_ICON_SZ / 2 + 1, RESULT_ICON_SZ / 2, RESULT_ICON_SZ,
                 s_res_ok ? UI_C_ACCENT : UI_C_DANGER);
}

/* ───────────────── 对外接口 ───────────────── */

void ui_main_build(lv_obj_t *parent)
{
    /* parent 可以是独立屏幕，也可以是 ui_stage 里的一个层 */
    s_scr = parent;
    lv_obj_remove_style_all(parent);
    lv_obj_set_size(parent, UI_W, UI_H);
    lv_obj_set_style_bg_color(parent, COL(UI_C_BG), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* 时钟：真实值由 user_clock 的 1s 定时器写入；未对时前显示占位符 */
    s_lbl_time = num_label(s_scr, 0, UI_TIME_Y, UI_W, UI_FONT_NUM44,
                           UI_C_TEXT, "--:--");

    /* 链路状态：右上角小圆点 + 字母（时钟居中，右上角是空的）*/
    s_link_dot = box(s_scr, UI_LINK_DOT_X, UI_LINK_DOT_Y, UI_LINK_DOT_SZ,
                     UI_LINK_DOT_SZ, UI_LINK_DOT_SZ / 2, UI_C_DIM, LV_OPA_COVER);
    s_link_txt = num_label(s_scr, UI_LINK_TXT_X, UI_LINK_TXT_Y, UI_LINK_TXT_W,
                           UI_FONT_NUM14, UI_C_DIM, "");
    s_link_shown = UI_LINK_NONE;

    /* 日期：纯数字，例如 "10/20"（不需要字库） */
    s_lbl_date = num_label(s_scr, 0, UI_DATE_Y, UI_W, UI_FONT_NUM14,
                           UI_C_DIM, "--/--");

    /* 结果回执：一张卡片 + 左意图图标 + 右状态图标 */
    lv_obj_t *res = box(s_scr, UI_RESULT_X, UI_RESULT_Y, UI_RESULT_W, UI_RESULT_H,
                        UI_RESULT_RADIUS, UI_C_CARD, LV_OPA_COVER);

    s_res_slot_intent = box(res, 8, (UI_RESULT_H - RESULT_ICON_SZ) / 2,
                            RESULT_ICON_SZ, RESULT_ICON_SZ, 0,
                            UI_C_CARD, LV_OPA_TRANSP);
    s_res_slot_status = box(res, UI_RESULT_W - 8 - RESULT_ICON_SZ,
                            (UI_RESULT_H - RESULT_ICON_SZ) / 2,
                            RESULT_ICON_SZ, RESULT_ICON_SZ, 0,
                            UI_C_CARD, LV_OPA_TRANSP);
    /* 回执文字：塞在两个图标中间的空位里（宽 152px ≈ 10 个汉字）*/
    s_lbl_reply = lv_label_create(res);
    lv_obj_set_pos(s_lbl_reply, 32, 7);   /* 字库墨迹在行框 0..14，y=7 时视觉居中 */
    lv_obj_set_size(s_lbl_reply, UI_RESULT_W - 64, RESULT_ICON_SZ);
    lv_obj_set_style_text_font(s_lbl_reply, &lv_font_cn_14, 0);
    lv_obj_set_style_text_color(s_lbl_reply, COL(UI_C_DIM), 0);
    lv_obj_set_style_text_align(s_lbl_reply, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_lbl_reply, LV_LABEL_LONG_DOT);   /* 太长省略，不换行 */
    lv_label_set_text(s_lbl_reply, "");

    result_repaint();

    /* 回执条 = 语音历史入口（不新增按钮，复用已有元素：
       用户看到"刚才那条结果"时，自然想点开看更多）*/
    lv_obj_add_flag(res, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(res, COL(UI_C_PRESSED), LV_STATE_PRESSED);
    lv_obj_add_event_cb(res, on_receipt_clicked, LV_EVENT_CLICKED, NULL);

    /* 3 个意图格：只放最常用的两个 + 「更多」
       （拓展指令都在抽屉里，避免和抽屉重复）*/
    tile(s_scr, UI_TILE_X0,                     UI_ICON_MUSIC,  UI_C_TEXT);  /* A01 播放/暂停 */
    tile(s_scr, UI_TILE_X0 + UI_TILE_STEP,      UI_ICON_NEXT,   UI_C_TEXT);  /* A02 下一首 */
    tile_more(s_scr, UI_TILE_X0 + UI_TILE_STEP * 2);

    /* 麦克风大按钮（无文字） */
    lv_obj_t *btn = box(s_scr, UI_BTN_X, UI_BTN_Y, UI_BTN_W, UI_BTN_H,
                        UI_BTN_RADIUS, UI_C_BTN, LV_OPA_COVER);
    lv_obj_set_style_border_color(btn, COL(UI_C_ACCENT), 0);
    lv_obj_set_style_border_width(btn, UI_BTN_BORDER, 0);
    ui_icon_draw(btn, UI_ICON_MIC, UI_BTN_W / 2, UI_BTN_H / 2, 34, UI_C_ACCENT);
    /* 按下时：底色变亮 + 描边加粗，手指能明显感到"按到了" */
    make_pressable(btn, on_talk_clicked, NULL);
    lv_obj_set_style_border_width(btn, UI_BTN_BORDER + 1, LV_STATE_PRESSED);
}

/** 独立屏幕版本（一般不用了：开机走 ui_stage）*/
lv_obj_t *ui_main_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    ui_main_build(scr);
    return scr;
}

void ui_main_set_intent_cb(ui_intent_cb_t cb)
{
    s_intent_cb = cb;
}

ui_intent_cb_t ui_main_get_intent_cb(void)
{
    return s_intent_cb;
}

void ui_main_set_text_cb(ui_text_cb_t cb)
{
    s_text_cb = cb;
}

ui_text_cb_t ui_main_get_text_cb(void)
{
    return s_text_cb;
}

void ui_main_set_mic_cb(ui_mic_cb_t cb)
{
    s_mic_cb = cb;
}

ui_mic_cb_t ui_main_get_mic_cb(void)
{
    return s_mic_cb;
}

void ui_main_set_result(ui_icon_t intent, bool ok)
{
    s_res_intent = intent;
    s_res_ok = ok;
    s_res_intent_col = ok ? UI_C_TEXT : UI_C_DIM;
    result_repaint();

    /* 抽屉可能正开着：把回执也送到它顶部的小反馈槽 */
    ui_drawer_set_result(intent, ok);

    /* 状态页正开着（语音流程）：转到全屏结果，1.2 秒后自动返回 */
    if (ui_status_visible()) {
        ui_status_show_result(intent, ok, UI_ST_HOLD_MS);
    }
}

void ui_main_set_reply(const char *text)
{
    if (!s_lbl_reply) return;
    lv_label_set_text(s_lbl_reply, (text && text[0]) ? text : "");
}

void ui_main_set_link(ui_link_t link)
{
    if (link == s_link_shown) return;      /* 状态没变就不重绘（可高频调用）*/
    s_link_shown = link;

    uint32_t col;
    const char *txt;
    switch (link) {
    case UI_LINK_USB: col = UI_C_ACCENT; txt = "U"; break;   /* 青：USB 在线 */
    case UI_LINK_BT:  col = UI_C_INFO;   txt = "B"; break;   /* 蓝：蓝牙在线 */
    default:          col = UI_C_DIM;    txt = "";  break;   /* 灰：离线 */
    }
    if (s_link_dot) lv_obj_set_style_bg_color(s_link_dot, COL(col), 0);
    if (s_link_txt) {
        lv_obj_set_style_text_color(s_link_txt, COL(col), 0);
        lv_label_set_text(s_link_txt, txt);
    }
}

void ui_main_set_time(const char *time_str, const char *date_str)
{
    if (s_lbl_time && time_str) lv_label_set_text(s_lbl_time, time_str);
    if (s_lbl_date && date_str) lv_label_set_text(s_lbl_date, date_str);
}
