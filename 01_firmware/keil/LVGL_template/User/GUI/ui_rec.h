/**
 * @file    ui_rec.h
 * @brief   录音页：波形 + 录音键 + 转写文字 + 发送前确认
 *
 * 这一页是"语音主流程"，设计目标是让用户**看清自己说了什么**再决定发送：
 *
 *   待机  →  按下录音键  →  录音中（波形跳动）
 *                               ↓ 松开/再按一次停止，等待识别
 *                        识别中（文字区显示 …）
 *                               ↓ 网关回 OV+EV=ASR,<文字>
 *                        已识别（显示转写文字）
 *                          ✗ 重录        ✓ 发送
 *                                        ↓ 走 T 慢通道 → 网关理解并执行
 *                                     回到主界面（回执走主界面横条）
 *
 * 当前阶段：麦克风还没接，波形由定时器模拟、文字由网关返回预设句子，
 * 但页面状态机与协议是真实完整的；接上 I2S 麦后只需替换波形数据源与音频上行。
 */
#ifndef UI_REC_H
#define UI_REC_H

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 进入录音页（从主界面麦克风按钮调用）。记住来源界面用于返回 */
void ui_rec_open(void);

/** 关闭录音页，回到进入前的界面 */
void ui_rec_close(void);

/** 网关返回的识别文字（在 LVGL 任务上下文调用，内部会刷新界面）*/
void ui_rec_set_text(const char *text);

/** 录音页当前是否可见 */
bool ui_rec_visible(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_REC_H */
