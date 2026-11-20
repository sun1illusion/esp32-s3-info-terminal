#ifndef __BOARD_AUDIO_H
#define __BOARD_AUDIO_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/* MAX98357A 音频输出（I2S1 TX）
 * 接线：DIN=GPIO7  BCLK=GPIO15  LRC=GPIO16  VIN=5V  SD 悬空
 *
 * 注意：这颗芯片没有数字音量，Audio_SetVolume 是对 PCM 采样做软件缩放。
 * 硬件增益由 GAIN 引脚焊接决定（悬空 9dB / GND 3dB / VDD 15dB），运行时不可改。 */

#define AUDIO_SAMPLE_RATE   22050

/* 初始化 I2S1 发送通道。重复调用安全（内部判空）。 */
esp_err_t Audio_Init(void);

/* 释放 I2S 通道 */
void Audio_Deinit(void);

/* 0~100，软件音量。0 为静音但仍输出静音帧（避免 DMA 欠载噪声） */
void Audio_SetVolume(int percent);
int  Audio_GetVolume(void);

/* 写一帧立体声 PCM（frames 为帧数，每帧左右各 1 个 int16）。
 * 内部按音量系数缩放后送入 I2S，阻塞直到写入完成。 */
esp_err_t Audio_WritePCM(const int16_t *stereo, size_t frames, uint32_t timeout_ms);

/* 播放单音（正弦波），用于链路验证。
 * 注意：阻塞 duration_ms，不要在 LVGL 任务或按键回调里直接调用。 */
esp_err_t Audio_PlayTone(int freq_hz, int duration_ms);

#endif
