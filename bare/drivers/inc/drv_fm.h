#pragma once
#include <stdint.h>
#include <stdbool.h>

/* FM 初始化配置 */
typedef struct {
    uint32_t frequency_khz;   /* 调谐频率 */
    uint8_t  rf_cap;          /* 射频电容校准值 */
} drv_fm_cfg_t;

/* 初始化 FM 模块 */
bool drv_fm_init(const drv_fm_cfg_t *cfg);

/* 开启 FM 播放 */
bool drv_fm_start(void);

/* 停止 FM 播放 */
void drv_fm_stop(void);

/* 关闭 FM 模块 */
void drv_fm_power_off(void);

/* 启动一次 FM DMA 到指定 buffer 的传输。完成状态需结合中断/硬件状态继续确认。 */
bool drv_fm_capture_to_buffer(int16_t *buf, uint32_t samples);

/* 使能/关闭 FM 到 audio buffer 的硬件通路 */
bool drv_fm_route_to_audio_buffer(bool enable);

/* 兼容旧接口：当前底层不是同步读接口，函数只启动 DMA 并返回 0。 */
uint32_t drv_fm_get_audio(int16_t *buf, uint32_t max_samples);

/* 同步 DAC / audio buffer 水位 */
void drv_fm_sync_dac(uint32_t samples_or_words);
