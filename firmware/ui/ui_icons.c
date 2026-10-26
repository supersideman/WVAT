/**
 * @file    ui_icons.c
 * @brief   纯图元图标库实现 · LVGL 8.2
 *
 * 约定：所有图标围绕 (cx, cy) 绘制，尺寸按 size 等比缩放
 *       → 同一个图标函数在 24px 和 34px 下都成立
 */
#include "ui_icons.h"
#include "ui_theme.h"

#define COL(c) lv_color_hex(c)

/* ── 图元工具 ─────────────────────────────────────────────── */

static lv_obj_t *put(lv_obj_t *p, int x, int y, int w, int h, int r, uint32_t color)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w < 1 ? 1 : w, h < 1 ? 1 : h);
    lv_obj_set_style_bg_color(o, COL(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *disc(lv_obj_t *p, int cx, int cy, int r, uint32_t color)
{
    return put(p, cx - r, cy - r, r * 2, r * 2, LV_RADIUS_CIRCLE, color);
}

static lv_obj_t *ring(lv_obj_t *p, int cx, int cy, int r, int w, uint32_t color)
{
    lv_obj_t *o = put(p, cx - r, cy - r, r * 2, r * 2, LV_RADIUS_CIRCLE, color);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(o, COL(color), 0);
    lv_obj_set_style_border_width(o, w, 0);
    return o;
}

/* lv_line 的点数组必须是静态的（LVGL 不拷贝） */
static lv_point_t s_tri[4];      /* 三角形 */
static lv_point_t s_line2[2];    /* 两点线 */

static lv_obj_t *polyline(lv_obj_t *p, lv_point_t *pts, uint16_t n,
                          int w, uint32_t color)
{
    lv_obj_t *l = lv_line_create(p);
    lv_line_set_points(l, pts, n);
    lv_obj_set_style_line_color(l, COL(color), 0);
    lv_obj_set_style_line_width(l, w, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    return l;
}

static int px(int size, int num, int den) { return (size * num) / den; }

/* ── 各图标 ───────────────────────────────────────────────── */

static void ic_music(lv_obj_t *p, int cx, int cy, int s, uint32_t c)
{
    int stem = s / 9;
    int hr = s / 7;
    disc(p, cx - px(s, 1, 5), cy + px(s, 1, 4), hr, c);        /* 符头 1 */
    disc(p, cx + px(s, 1, 4), cy + px(s, 1, 8), hr, c);        /* 符头 2 */
    put(p, cx - px(s, 1, 5) - stem / 2, cy - px(s, 1, 4), stem, px(s, 1, 2), 0, c);
    put(p, cx + px(s, 1, 4) - stem / 2, cy - px(s, 1, 5), stem, px(s, 1, 2), 0, c);
    put(p, cx - px(s, 1, 5) - stem / 2, cy - px(s, 1, 4),
        px(s, 1, 2) + stem, stem, 0, c);                        /* 横梁 */
}

static void ic_moon(lv_obj_t *p, int cx, int cy, int s, uint32_t c)
{
    /* 实心圆 + 卡片底色圆挖缺 → 月牙 */
    int r = px(s, 2, 5);
    disc(p, cx, cy, r, c);
    disc(p, cx + px(s, 1, 5), cy - px(s, 1, 8), r, UI_C_CARD);
}

static void ic_folder(lv_obj_t *p, int cx, int cy, int s, uint32_t c)
{
    put(p, cx - px(s, 1, 2), cy - px(s, 1, 4), s, px(s, 3, 5), s / 12, c);   /* 主体 */
    put(p, cx - px(s, 1, 2), cy - px(s, 1, 2) - px(s, 1, 8),
        px(s, 2, 5), px(s, 1, 8), s / 14, c);                                /* 折页 */
}

static void ic_grid(lv_obj_t *p, int cx, int cy, int s, uint32_t c)
{
    int q = px(s, 2, 5), gap = px(s, 1, 10), r = s / 12;
    put(p, cx - q - gap / 2, cy - q - gap / 2, q, q, r, c);
    put(p, cx + gap / 2,     cy - q - gap / 2, q, q, r, c);
    put(p, cx - q - gap / 2, cy + gap / 2,     q, q, r, c);
    put(p, cx + gap / 2,     cy + gap / 2,     q, q, r, c);
}

static void ic_mic(lv_obj_t *p, int cx, int cy, int s, uint32_t c)
{
    int bw = s / 3;                              /* 话筒帽宽 */
    int bh = s / 2;                              /* 话筒帽高 */
    int barw = s / 12 > 2 ? s / 12 : 2;          /* 线粗 */
    int sdx = (s * 30) / 100;                    /* U 型左右偏移 */
    int sy = cy - (s * 12) / 100;                /* U 型顶 */
    int sh = (s * 26) / 100;                     /* U 型高 */

    /* 话筒帽 */
    put(p, cx - bw / 2, cy - (s * 43) / 100, bw, bh, bw / 2, c);
    /* U 型支架：左竖 + 右竖 + 底横 */
    put(p, cx - sdx, sy, barw, sh, barw / 2, c);
    put(p, cx + sdx - barw, sy, barw, sh, barw / 2, c);
    put(p, cx - sdx, sy + sh - barw, sdx * 2, barw, barw / 2, c);
    /* 支杆 */
    put(p, cx - barw / 2, sy + sh, barw, (s * 14) / 100, barw / 2, c);
}

static void ic_next(lv_obj_t *p, int cx, int cy, int s, uint32_t c)
{
    int h = px(s, 3, 5), w = px(s, 1, 3);
    s_tri[0].x = cx - w;     s_tri[0].y = cy - h / 2;
    s_tri[1].x = cx + w / 2; s_tri[1].y = cy;
    s_tri[2].x = cx - w;     s_tri[2].y = cy + h / 2;
    s_tri[3] = s_tri[0];
    polyline(p, s_tri, 4, s / 10 > 1 ? s / 10 : 2, c);
    put(p, cx + w / 2 + 2, cy - h / 2, s / 12 > 2 ? s / 12 : 2, h, 0, c);
}

static void ic_vol(lv_obj_t *p, int cx, int cy, int s, uint32_t c)
{
    int h = px(s, 1, 2);
    s_tri[0].x = cx - px(s, 2, 5); s_tri[0].y = cy - px(s, 1, 8);
    s_tri[1].x = cx - px(s, 1, 10); s_tri[1].y = cy;
    s_tri[2].x = cx - px(s, 2, 5); s_tri[2].y = cy + px(s, 1, 8);
    s_tri[3] = s_tri[0];
    polyline(p, s_tri, 4, s / 10 > 1 ? s / 10 : 2, c);
    ring(p, cx + px(s, 1, 6), cy, px(s, 1, 4), s / 12 > 1 ? s / 12 : 1, c);
}

static void ic_mute(lv_obj_t *p, int cx, int cy, int s, uint32_t c)
{
    ic_vol(p, cx - px(s, 1, 10), cy, s, c);
    int d = px(s, 1, 4), o = s / 12 > 1 ? s / 12 : 2;
    s_line2[0].x = cx + px(s, 1, 6) - d; s_line2[0].y = cy - d;
    s_line2[1].x = cx + px(s, 1, 6) + d; s_line2[1].y = cy + d;
    polyline(p, s_line2, 2, o, c);
    s_line2[0].x = cx + px(s, 1, 6) + d; s_line2[0].y = cy - d;
    s_line2[1].x = cx + px(s, 1, 6) - d; s_line2[1].y = cy + d;
    polyline(p, s_line2, 2, o, c);
}

static void ic_list(lv_obj_t *p, int cx, int cy, int s, uint32_t c)
{
    int y0 = -px(s, 1, 4), dy = px(s, 1, 4), r = s / 12 > 1 ? s / 12 : 1;
    for (int i = 0; i < 3; i++) {
        int y = cy + y0 + i * dy;
        disc(p, cx - px(s, 2, 5), y, r, c);
        put(p, cx - px(s, 1, 6), y - r / 2 > 0 ? y - r / 2 : y,
            px(s, 3, 5), r * 2 > 2 ? r * 2 : 2, 0, c);
    }
}

static void ic_gear(lv_obj_t *p, int cx, int cy, int s, uint32_t c)
{
    int r = px(s, 1, 4), w = s / 10 > 2 ? s / 10 : 2;
    ring(p, cx, cy, r, w, c);
    disc(p, cx, cy, s / 12 > 1 ? s / 12 : 1, c);
    /* 8 个齿：用上下左右 + 四个斜角的小方块近似（避免三角函数依赖） */
    int t = s / 10 > 2 ? s / 10 : 2, L = px(s, 1, 2);
    put(p, cx - t / 2, cy - L, t, t, 0, c);
    put(p, cx - t / 2, cy + L - t, t, t, 0, c);
    put(p, cx - L, cy - t / 2, t, t, 0, c);
    put(p, cx + L - t, cy - t / 2, t, t, 0, c);
    int d = px(s, 1, 3);
    put(p, cx - d, cy - d, t, t, 0, c);
    put(p, cx + d - t, cy - d, t, t, 0, c);
    put(p, cx - d, cy + d - t, t, t, 0, c);
    put(p, cx + d - t, cy + d - t, t, t, 0, c);
}

static void ic_ok(lv_obj_t *p, int cx, int cy, int s, uint32_t c)
{
    s_line2[0].x = cx - px(s, 1, 3); s_line2[0].y = cy;
    s_line2[1].x = cx - px(s, 1, 12); s_line2[1].y = cy + px(s, 1, 4);
    polyline(p, s_line2, 2, s / 7 > 2 ? s / 7 : 3, c);
    s_line2[0].x = cx - px(s, 1, 12); s_line2[0].y = cy + px(s, 1, 4);
    s_line2[1].x = cx + px(s, 1, 3); s_line2[1].y = cy - px(s, 1, 3);
    polyline(p, s_line2, 2, s / 7 > 2 ? s / 7 : 3, c);
}

static void ic_cross(lv_obj_t *p, int cx, int cy, int s, uint32_t c)
{
    int d = px(s, 1, 4), w = s / 7 > 2 ? s / 7 : 3;
    s_line2[0].x = cx - d; s_line2[0].y = cy - d;
    s_line2[1].x = cx + d; s_line2[1].y = cy + d;
    polyline(p, s_line2, 2, w, c);
    s_line2[0].x = cx + d; s_line2[0].y = cy - d;
    s_line2[1].x = cx - d; s_line2[1].y = cy + d;
    polyline(p, s_line2, 2, w, c);
}

/* ── 总入口 ───────────────────────────────────────────────── */

void ui_icon_draw(lv_obj_t *parent, ui_icon_t kind, int cx, int cy, int size,
                  uint32_t color)
{
    switch (kind) {
    case UI_ICON_MUSIC:  ic_music(parent, cx, cy, size, color);  break;
    case UI_ICON_MOON:   ic_moon(parent, cx, cy, size, color);   break;
    case UI_ICON_FOLDER: ic_folder(parent, cx, cy, size, color); break;
    case UI_ICON_GRID:   ic_grid(parent, cx, cy, size, color);   break;
    case UI_ICON_MIC:    ic_mic(parent, cx, cy, size, color);    break;
    case UI_ICON_NEXT:   ic_next(parent, cx, cy, size, color);   break;
    case UI_ICON_VOL:    ic_vol(parent, cx, cy, size, color);    break;
    case UI_ICON_MUTE:   ic_mute(parent, cx, cy, size, color);   break;
    case UI_ICON_LIST:   ic_list(parent, cx, cy, size, color);   break;
    case UI_ICON_GEAR:   ic_gear(parent, cx, cy, size, color);   break;
    case UI_ICON_OK:     ic_ok(parent, cx, cy, size, color);     break;
    case UI_ICON_CROSS:  ic_cross(parent, cx, cy, size, color);  break;
    default: break;
    }
}
