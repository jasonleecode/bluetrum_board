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

/* 读取音频数据到 buffer */
uint32_t drv_fm_get_audio(int16_t *buf, uint32_t max_samples);

/* 同步 DAC（可选） */
void drv_fm_sync_dac(void);