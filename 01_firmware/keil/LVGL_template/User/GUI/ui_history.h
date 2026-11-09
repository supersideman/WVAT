/**
 * @file    ui_history.h
 * @brief   语音历史页：看"刚才说过什么、结果如何"
 *
 * 设计意图
 * - 让手表像个"助手"而不是"遥控器"：能看到刚才的对话记录。
 * - 入口不新增按钮：主界面上那条**结果回执**就是入口，点它进历史。
 * - 目前存内存（8 条），掉电即失；将来可落到板载 W25Q128（16MB）做持久化。
 */
#ifndef UI_HISTORY_H
#define UI_HISTORY_H

#include <stdbool.h>

#include "lvgl.h"
#include "ui_theme.h"

/** 最多保留几条记录（8 条 × 约 52 字节 ≈ 416 字节 RAM） */
#define UI_HIST_MAX        8
/** 单条文字上限（UTF-8 字节）。协议 MAX_TEXT_BYTES=60，这里留余量便于截断 */
#define UI_HIST_TEXT_MAX   52

/** 新增一条记录（状态先记为"等待中"，收到回执后再更新） */
void ui_history_add(const char *text);

/** 更新最近一条记录的执行结果（true=成功 / false=失败） */
void ui_history_mark_last(bool ok);

/** 打开 / 关闭历史页 */
void ui_history_open(void);
void ui_history_close(void);

/** 历史页是否可见 */
bool ui_history_visible(void);

#endif /* UI_HISTORY_H */
