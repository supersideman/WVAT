/**
 * @file    user_i2s.h
 * @brief   I2S2 数字麦克风采集（INMP441 / ICS-43434 一类）
 *
 * 两套可用接线（固件开机会自动探测哪套通）：
 *
 *   主用（AF5）：INMP441 SCK→PB13  WS→PB12  SD→PB15
 *   备用（AF6）：INMP441 SCK→PB10  WS→PB9   SD→PC3
 *
 *   两套都需要：VDD→3V3（⚠️ 绝不能接 5V）、GND→GND、L/R→GND（选左声道）
 *   备用组合的好处：CK/SD 在扩展板 2×9 上，只需为 WS 焊一根线。
 *
 * 时钟方案（按本板 25MHz HSE、PLLM=12 计算）：
 *   PLLI2S: N=58, R=2  → I2S 时钟 60.417 MHz
 *   16kHz / 24bit / Philips → SCK = 1.024 MHz，I2SDIV=29 + ODD=1
 *   实际采样率 16000.2 Hz（误差 0.001%）
 */
#ifndef USER_I2S_H
#define USER_I2S_H

#include <stdbool.h>
#include <stdint.h>

/** 单帧的 32bit 字数（左声道 + 右声道各一个 word） */
#define MIC_WORDS_PER_FRAME 2u

/** 开机是否自动跑麦克风自检（确认硬件时开，正常使用可置 0）*/
#define MIC_SELFTEST_ON_BOOT 1

/** 初始化 PLLI2S + I2S2 + GPIO，成功后即可读样本 */
bool mic_init(void);

/** 读一组立体声样本（words = 32bit 字数，必须是 2 的倍数） */
bool mic_read_raw(uint32_t *buf, uint16_t words, uint32_t timeout_ms);

/**
 * 开机自检：采 1 秒音频，统计峰值/RMS 并通过 USART1 打印。
 * 输出格式：[mic] peak=... rms=... n=...
 * 用来判断接线是否正确，不需要上位机配合。
 */
void mic_boot_selftest(void);

/** 当前生效的引脚组合名称（自检会打印到串口） */
const char *mic_active_pinset(void);

/** 最近一次自检得到的峰值（0 表示没采到声音） */
uint32_t mic_last_peak(void);

#endif /* USER_I2S_H */
