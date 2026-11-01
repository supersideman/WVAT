/**
 * @file    ui_theme.h
 * @brief   配色与布局常量（唯一事实来源）· 图形化版本 v2
 *
 * 设计原则：**界面不放中文**
 *   - 意图用图标表达，状态用「颜色 + 形状」表达
 *   - 只保留数字（时钟/日期），用 LVGL 内置 Montserrat
 *   → 整套 UI 零中文字库，省 30~80KB Flash，也免去字库生成的麻烦
 *
 * 目标：FryPi / STM32F411RET6 + LVGL 8.2 + 240x280 ST7789
 */
#ifndef UI_THEME_H
#define UI_THEME_H

/* ── 屏幕 ─────────────────────────────────────────────── */
#define UI_W            240
#define UI_H            280

/* ── 配色 ─────────────────────────────────────────────── */
#define UI_C_BG         0x0E1116
#define UI_C_CARD       0x1B2129
#define UI_C_BTN        0x0A1218
#define UI_C_TEXT       0xE8EDF2
#define UI_C_DIM        0x5C6B7A   /* 图标常态色（比 v1 更暗，避免抢视线）*/
#define UI_C_PRESSED    0x2E3947   /* 按下态：比卡片亮一档，做触摸反馈 */
#define UI_C_ACCENT     0x35D0BA
#define UI_C_DANGER     0xFF5C5C
#define UI_C_WARN       0xFFC24B
#define UI_C_INFO       0x4A9EFF

/* ── 字体：只保留数字 ──────────────────────────────────── */
#ifndef UI_FONT_NUM44
#define UI_FONT_NUM44   (&lv_font_montserrat_44)   /* 时钟 */
#endif
#ifndef UI_FONT_NUM14
#define UI_FONT_NUM14   (&lv_font_montserrat_14)   /* 日期 */
#endif

/* ── 主界面布局 ───────────────────────────────────────── */
#define UI_TIME_Y        6    /* 时钟文字顶边 */
#define UI_DATE_Y        64   /* 日期文字顶边 */
#define UI_RESULT_X      12
#define UI_RESULT_Y      84
#define UI_RESULT_W      216
#define UI_RESULT_H      28
#define UI_RESULT_RADIUS 8
#define UI_TILE_Y        122
#define UI_TILE_X0       12
#define UI_TILE_STEP     75   /* 66 宽 + 9 间距 */
#define UI_TILE_SIZE     66
#define UI_TILE_RADIUS   14
#define UI_ICON_CY       33   /* 图标中心（相对格子上边）*/
#define UI_BTN_X         12
#define UI_BTN_Y         198
#define UI_BTN_W         216
#define UI_BTN_H         56
#define UI_BTN_RADIUS    14
#define UI_BTN_BORDER    2

/* ── 状态条（过渡页用）───────────────────────────────── */
#define UI_STATUS_H      44   /* 顶部状态区高度 */
#define UI_HINT_H        36   /* 底部提示条高度 */

/* ── 抽屉页（「更多」：只放拓展指令，每行 3 格 × 2 行 = 6 格）─────
   主界面已经有「音乐播放/暂停」「下一首」，所以抽屉不再重复它们。 */
#define UI_DRW_TOP_H     44   /* 顶部条：左=返回键，右=操作反馈 */
#define UI_DRW_X0        12
#define UI_DRW_Y0        97   /* 两行在剩余区域里垂直居中：44+(236-130)/2 */
#define UI_DRW_STEP_X    75   /* 66 宽 + 9 间距 */
#define UI_DRW_STEP_Y    70   /* 60 高 + 10 间距 */
#define UI_DRW_W         66
#define UI_DRW_H         60
#define UI_DRW_RADIUS    14
#define UI_DRW_ICON_CY   30   /* 图标中心（相对格子上边）*/
#define UI_DRW_ICON_SZ   28
#define UI_BACK_X        10   /* 返回键（顶部条左侧）*/
#define UI_BACK_Y        8
#define UI_BACK_SIZE     28
/* 顶部条右侧的小反馈槽：左=刚点的意图图标，右=✓/✗ */
#define UI_DRW_FB_W      56
#define UI_DRW_FB_H      28
#define UI_DRW_FB_X      (UI_W - 12 - UI_DRW_FB_W)
#define UI_DRW_FB_Y      ((UI_DRW_TOP_H - UI_DRW_FB_H) / 2)
#define UI_DRW_FB_TIMEOUT_MS 3000   /* 超时没回执就显示 ✗（网关没开/掉线/指令丢了）*/

/* ── 锁屏页 ───────────────────────────────────────────── */
#define UI_LOCK_TIME_Y   96    /* 大时钟文字顶边 */
#define UI_LOCK_DATE_Y   152
#define UI_LOCK_SWIPE_PX 80    /* 上滑多少像素算解锁；30 太灵敏，轻碰一下就进去了 */
#define UI_LOCK_HOLD_MS  1200  /* 长按兜底解锁（防手势失效被卡在锁屏）*/

#endif /* UI_THEME_H */
