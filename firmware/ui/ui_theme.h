/**
 * @file    ui_theme.h
 * @brief   配色与布局常量（唯一事实来源）
 *
 * 改样式只改这里。design/render_c_preview.py 里的同名常量与本文保持一致，
 * 所以「图」和「代码」永远是同一个东西。
 *
 * 目标：FryPi / STM32F411RET6 + LVGL 8.2 + 240x280 ST7789
 */
#ifndef UI_THEME_H
#define UI_THEME_H

/* ── 屏幕 ─────────────────────────────────────────────── */
#define UI_W            240
#define UI_H            280

/* ── 配色（全部 RGB565 安全区，纯色不用渐变）───────────── */
#define UI_C_BG         0x0E1116   /* 背景 */
#define UI_C_CARD       0x1B2129   /* 卡片 */
#define UI_C_BTN        0x0A1218   /* 主按钮底 */
#define UI_C_TEXT       0xE8EDF2   /* 主文字 */
#define UI_C_DIM        0x8A96A3   /* 次文字 */
#define UI_C_ACCENT     0x35D0BA   /* 青：成功/强调 */
#define UI_C_DANGER     0xFF5C5C   /* 红：录音/失败 */
#define UI_C_WARN       0xFFC24B   /* 黄：识别中 */
#define UI_C_INFO       0x4A9EFF   /* 蓝：处理中 */

/* ── 字体 ─────────────────────────────────────────────── */
/* 中文字库需要自己生成（lv_font_conv），用宏留出替换点：
 * 生成后把下面三行换成你自己的 lv_font_t 指针即可。 */
#ifndef UI_FONT_NUM44
#define UI_FONT_NUM44   (&lv_font_montserrat_44)  /* 时钟数字 */
#endif
#ifndef UI_FONT_CN16
#define UI_FONT_CN16    (&lv_font_montserrat_16)  /* 正文（中文替换点）*/
#endif
#ifndef UI_FONT_CN12
#define UI_FONT_CN12    (&lv_font_montserrat_12)  /* 小字（中文替换点）*/
#endif

/* ── 主界面布局（与设计稿 §4.4 一致）──────────────────── */
#define UI_TIME_Y        8    /* 时钟文字顶边 */
#define UI_DATE_Y        74   /* 日期文字顶边 */
#define UI_RESULT_X      12
#define UI_RESULT_Y      96
#define UI_RESULT_W      216
#define UI_RESULT_H      26
#define UI_TILE_Y        132
#define UI_TILE_X0       12
#define UI_TILE_STEP     56   /* 48 宽 + 8 间距 */
#define UI_TILE_SIZE     48
#define UI_TILE_RADIUS   10
#define UI_BTN_X         12
#define UI_BTN_Y         196
#define UI_BTN_W         216
#define UI_BTN_H         52
#define UI_BTN_RADIUS    12
#define UI_BTN_BORDER    2

/* ── 通用尺寸 ─────────────────────────────────────────── */
#define UI_CARD_RADIUS   10
#define UI_RESULT_RADIUS 8
#define UI_STATUS_H      40   /* 状态条高度（过渡页用）*/
#define UI_HINT_H        36   /* 底部提示条高度（过渡页用）*/

#endif /* UI_THEME_H */
