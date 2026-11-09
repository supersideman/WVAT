/**
 * @file    ui_history.c
 * @brief   语音历史页实现 · LVGL 8.2
 *
 * 结构
 * - 顶部条（26px）：左返回 + 居中标题「历史」
 * - 列表容器（可滚动）：每行 44px，行内 = 时间 + 说话内容 + 结果圆点
 * - 空状态：居中显示「还没有记录」
 *
 * 为什么存在内存而不是 Flash
 *   8 条 × 约 52 字节 ≈ 416 字节，对 128KB RAM 完全无压力；
 *   写 Flash 需要 W25Q128 驱动 + 掉电保护，等真的有"翻旧账"需求再做。
 */
#include "ui_history.h"

#include <string.h>

#include "ui_icons.h"
#include "user_clock.h"

#define COL(c) lv_color_hex(c)

LV_FONT_DECLARE(lv_font_cn_14);

/* 记录状态 */
#define ST_WAIT 0
#define ST_OK   1
#define ST_ERR  2

typedef struct {
    char    time[6];                  /* "HH:MM" */
    uint8_t state;
    char    text[UI_HIST_TEXT_MAX];   /* UTF-8 */
} hist_item_t;

static hist_item_t s_items[UI_HIST_MAX];
static int         s_count = 0;

static lv_obj_t *s_scr      = NULL;
static lv_obj_t *s_prev_scr = NULL;
static lv_obj_t *s_list     = NULL;    /* 行容器 */
static lv_obj_t *s_empty    = NULL;    /* 空状态提示 */
static bool      s_visible  = false;

static void refresh_list(void);   /* 前向声明：add/mark 里要实时刷新 */

/* ───────────────── 工具 ───────────────── */

static lv_obj_t *hbox(lv_obj_t *parent, int x, int y, int w, int h, int radius,
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

static lv_obj_t *hline(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_size(l, w, h);
    lv_obj_set_style_text_font(l, &lv_font_cn_14, 0);
    lv_obj_set_style_text_color(l, COL(color), 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);   /* 太长就省略，不换行 */
    lv_label_set_text(l, "");
    return l;
}

/* ───────────────── 数据 ───────────────── */

static void copy_text(char *dst, size_t cap, const char *src)
{
    size_t n = 0;
    if (!src) { dst[0] = '\0'; return; }
    while (src[n] != '\0' && n < cap - 1) n++;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void ui_history_add(const char *text)
{
    if (!text || text[0] == '\0') return;

    /* 满了就把最旧的挤出去（8 条，成本可忽略） */
    if (s_count >= UI_HIST_MAX) {
        memmove(&s_items[0], &s_items[1], sizeof(hist_item_t) * (UI_HIST_MAX - 1));
        s_count = UI_HIST_MAX - 1;
    }

    hist_item_t *it = &s_items[s_count++];
    const char *ts = clock_time_str();
    copy_text(it->time, sizeof(it->time), (ts && ts[0] && ts[0] != '-') ? ts : "--:--");
    it->state = ST_WAIT;
    copy_text(it->text, sizeof(it->text), text);

    if (s_visible) refresh_list();   /* 页面开着就实时刷新 */
}

void ui_history_mark_last(bool ok)
{
    if (s_count <= 0) return;
    s_items[s_count - 1].state = ok ? ST_OK : ST_ERR;
    if (s_visible) refresh_list();
}

/* ───────────────── 渲染 ───────────────── */

static void refresh_list(void)
{
    if (!s_list) return;

    lv_obj_clean(s_list);      /* 8 行以内，重建比增量更新简单可靠 */

    if (s_count == 0) {
        if (s_empty) lv_obj_clear_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (s_empty) lv_obj_add_flag(s_empty, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < s_count; i++) {
        hist_item_t *it = &s_items[i];
        int y = 6 + i * (UI_HIST_ROW_H + UI_HIST_ROW_GAP);

        lv_obj_t *row = hbox(s_list, 0, y, UI_HIST_W, UI_HIST_ROW_H, 12,
                             UI_C_CARD, LV_OPA_COVER);

        /* 时间（等宽数字，避免行间抖动）*/
        lv_obj_t *t = lv_label_create(row);
        lv_obj_set_pos(t, 8, 14);
        lv_obj_set_style_text_font(t, UI_FONT_NUM14, 0);
        lv_obj_set_style_text_color(t, COL(UI_C_DIM), 0);
        lv_label_set_text(t, it->time);

        /* 说话内容 */
        lv_obj_t *msg = hline(row, 50, 12, UI_HIST_W - 50 - 24, 20, UI_C_TEXT);
        lv_label_set_text(msg, it->text);

        /* 结果圆点：灰=等待，青=成功，红=失败 */
        uint32_t dot = (it->state == ST_OK)  ? UI_C_ACCENT
                     : (it->state == ST_ERR) ? UI_C_DANGER
                                             : UI_C_DIM;
        hbox(row, UI_HIST_W - 18, (UI_HIST_ROW_H - 8) / 2, 8, 8,
             LV_RADIUS_CIRCLE, dot, LV_OPA_COVER);
    }
}

static void on_back_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_history_close();
}

static void build_screen(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_scr);
    lv_obj_set_size(s_scr, UI_W, UI_H);
    lv_obj_set_style_bg_color(s_scr, COL(UI_C_BG), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* 顶部条 */
    hbox(s_scr, 0, 0, UI_W, UI_HIST_TOP_H, 0, UI_C_CARD, LV_OPA_COVER);

    /* 返回键：**看得见的按钮小，能点的区域大**。
       24px 的按钮手指几乎点不准（踩过），所以外面套一个透明的 56×46 热区，
       两个对象都回调同一个函数。 */
    lv_obj_t *hit = hbox(s_scr, 0, 0, 56, 46, 0, UI_C_CARD, LV_OPA_TRANSP);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(hit, on_back_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back = hbox(hit, 3, 3, UI_HIST_BACK_SIZE, UI_HIST_BACK_SIZE, 10,
                          UI_C_CARD, LV_OPA_COVER);
    ui_icon_draw(back, UI_ICON_BACK, UI_HIST_BACK_SIZE / 2, UI_HIST_BACK_SIZE / 2,
                 UI_HIST_BACK_SIZE - 4, UI_C_TEXT);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);          /* 按在图标上也有反馈 */
    lv_obj_set_style_bg_color(back, COL(UI_C_PRESSED), LV_STATE_PRESSED);
    lv_obj_add_event_cb(back, on_back_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *title = hline(s_scr, 0, (UI_HIST_TOP_H - 19) / 2 + 2, UI_W, 20, UI_C_TEXT);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
    lv_label_set_text(title, "历史");

    /* 列表容器：可滚动 */
    s_list = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_pos(s_list, UI_HIST_X, UI_HIST_TOP_H + 2);
    lv_obj_set_size(s_list, UI_HIST_W, UI_H - UI_HIST_TOP_H - 6);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);

    /* 空状态 */
    s_empty = hline(s_scr, 0, UI_H / 2 - 10, UI_W, 20, UI_C_DIM);
    lv_obj_set_style_text_align(s_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_empty, LV_LABEL_LONG_CLIP);
    lv_label_set_text(s_empty, "还没有记录");

    refresh_list();
}

/* ───────────────── 对外接口 ───────────────── */

void ui_history_open(void)
{
    if (!s_scr) build_screen();

    if (!s_visible) {
        s_prev_scr = lv_scr_act();
        refresh_list();
        lv_scr_load_anim(s_scr, LV_SCR_LOAD_ANIM_FADE_ON, 120, 0, false);
        s_visible = true;
    }
}

void ui_history_close(void)
{
    if (!s_visible) return;

    s_visible = false;
    lv_obj_t *back = s_prev_scr ? s_prev_scr : lv_scr_act();
    s_prev_scr = NULL;
    if (back && back != s_scr) {
        lv_scr_load_anim(back, LV_SCR_LOAD_ANIM_FADE_ON, 120, 0, false);
    }
}

bool ui_history_visible(void)
{
    return s_visible;
}
