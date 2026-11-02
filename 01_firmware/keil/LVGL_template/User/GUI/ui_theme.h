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
/* 顶部条右侧的小反馈：意图图标 + 紧挨着的圆点（同一行、留一点间隔）
   ⚠️ 图标位置固定、不随圆点出现而移动，避免"有结果时图标跳一下" */
#define UI_DRW_FB_ICON_SZ 18
#define UI_DRW_FB_GAP     8
#define UI_DRW_FB_X      (UI_W - 14 - (UI_DRW_FB_ICON_SZ + UI_DRW_FB_GAP + UI_DRW_DOT_SZ))
#define UI_DRW_FB_Y      ((UI_DRW_TOP_H - UI_DRW_FB_ICON_SZ) / 2)
#define UI_DRW_FB_TIMEOUT_MS 3000   /* 超时没回执就显示红点（网关没开/掉线/指令丢了）*/
/* 结果指示用实心圆点：绿=成功，红=失败，不显示=等待中（比 ✓/✗ 干净）*/
#define UI_DRW_DOT_SZ    12
#define UI_DRW_DOT_X     (UI_DRW_FB_X + UI_DRW_FB_ICON_SZ + UI_DRW_FB_GAP)
#define UI_DRW_DOT_Y     ((UI_DRW_TOP_H - UI_DRW_DOT_SZ) / 2)

/* ── 锁屏页 ───────────────────────────────────────────── */
#define UI_LOCK_TIME_Y   96    /* 大时钟文字顶边 */
#define UI_LOCK_DATE_Y   152
#define UI_LOCK_SWIPE_PX 80    /* 上滑多少像素算解锁；30 太灵敏，轻碰一下就进去了 */
#define UI_LOCK_HOLD_MS  1200  /* 长按兜底解锁（防手势失效被卡在锁屏）*/

/* ── 状态页（全屏反馈，服务语音流程）────────────────────── */
#define UI_ST_CY         118   /* 主图形中心 y */
#define UI_ST_BARS       13    /* 录音波形条数 */
#define UI_ST_BAR_W      4
#define UI_ST_BAR_GAP    9     /* 相邻条中心距 */
#define UI_ST_BAR_MIN_H  10
#define UI_ST_BAR_MAX_H  36
#define UI_ST_DOT_R      6     /* 录音时顶部的红点半径 */
#define UI_ST_DOT_Y      44
#define UI_ST_RING_R     44    /* 结果页外圈半径 */
#define UI_ST_RING_W     3
#define UI_ST_ICON_SZ    56    /* 对勾/叉的尺寸 */
#define UI_ST_HOLD_MS    1200  /* 结果显示多久后自动返回 */
#define UI_ST_REC_MAX_MS 5000  /* 录音状态最多停留（没回执也要有结局）*/

/* ── 录音页（语音主流程）──────────────────────────────────
   布局：顶部返回 | 波形 | 转写文字 | 底部按钮 */
#define UI_REC_TOP_H     26    /* 顶部条高度 */
#define UI_REC_BACK_SIZE 24
#define UI_REC_WAVE_CY   64    /* 波形中心 y */
#define UI_REC_WAVE_BARS 13
#define UI_REC_WAVE_W    4     /* 单条宽 */
#define UI_REC_WAVE_GAP  9     /* 条间距（中心距）*/
#define UI_REC_WAVE_HMIN 6     /* 静息高度 */
#define UI_REC_WAVE_HMAX 34    /* 最大高度 */
#define UI_REC_TEXT_Y    100   /* 转写文字区顶边 */
#define UI_REC_TEXT_H    84    /* 文字区高度（约 4 行 16px）*/
#define UI_REC_TEXT_X    14
#define UI_REC_TEXT_W    (UI_W - 2 * UI_REC_TEXT_X)
#define UI_REC_BTN_Y     196   /* 底部按钮区顶边 */
#define UI_REC_BTN_H     56
#define UI_REC_MIC_SIZE  64    /* 录音大圆键直径 */
#define UI_REC_OK_W      96    /* 确认/取消按钮尺寸 */
#define UI_REC_OK_GAP    16

/* 录音时长限制
   · 上限 10 秒：语音指令 2~5 秒够用，10 秒是宽容上限；
     真正的约束在链路——16kHz/16bit 音频要 32KB/s，串口必须提到 460800。
   · 下限 0.3 秒：太短视为误触，直接丢弃，不发给网关。
   · 剩余时间用**环绕录音键的圆环**表示（走完一圈=到上限），
     不用数字——数字要占顶部空间，还挤到了返回键。*/
#define UI_REC_MAX_MS    10000
#define UI_REC_MIN_MS    300

/* 环绕录音键的倒计时圆环 */
#define UI_REC_ARC_SIZE  80
#define UI_REC_ARC_WIDTH 4
#define UI_REC_ARC_X     (UI_W / 2 - UI_REC_ARC_SIZE / 2)
#define UI_REC_ARC_Y     (UI_REC_BTN_Y + UI_REC_MIC_SIZE / 2 - UI_REC_ARC_SIZE / 2)

/* 转写文字显示上限：文字区 4 行 × 约 15 字 ≈ 60 汉字。
   协议 MAX_TEXT_BYTES=60（20 汉字）更严格，所以正常不会溢出；
   超过 UI_REC_TEXT_MAX 个字符时直接截断，避免文字溢出到按钮区。*/
#define UI_REC_TEXT_MAX  60

#endif /* UI_THEME_H */
