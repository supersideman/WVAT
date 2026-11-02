/**
 * @file    user_link.h
 * @brief   手表 ↔ PC 网关的指令链路（走 USART1 / CH340N，115200 8N1）
 *
 * 协议（与 PC 端 gateway.py 完全一致）：
 *   手表 → PC   OV+AI=<REQ_ID>,<CODE>\r\n
 *   PC → 手表   OV+AR=<REQ_ID>,<STATUS>,<TEXT>\r\n
 *
 * 设计要点
 * - 串口中断只负责"收字节、攒成行、置标志"，**绝不在中断里碰 LVGL**
 * - LVGL 更新由 lv_timer 在 LVGL 任务上下文里做（30ms 轮询一次）
 * - 因此不需要改动作者的 Task 文件
 */
#ifndef USER_LINK_H
#define USER_LINK_H

#include <stdbool.h>
#include <stdint.h>

#include "ui_icons.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化：注册意图回调 + 启动 USART1 接收中断 + 建轮询定时器 */
void link_init(void);

/** 主动发一条意图（UI 的意图回调会调它） */
void link_send_intent(ui_icon_t kind);

/**
 * 发一段自由文本（录音页确认后调用）
 * 走 OV+AI=<id>,T,<文字>，由网关的慢通道理解并执行。
 */
void link_send_text(const char *text);

/** 通知网关开始/结束录音（A08,start / A08,stop）*/
void link_send_mic_ctrl(bool start);

/** 图标 → 协议动作码（A01/A03/A07/A08） */
const char *link_code_of(ui_icon_t kind);

#ifdef __cplusplus
}
#endif

#endif /* USER_LINK_H */
