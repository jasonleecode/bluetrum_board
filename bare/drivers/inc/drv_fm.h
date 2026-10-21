#pragma once
#include <stdint.h>
#include <stdbool.h>

/* FM 初始化配置 */
typedef struct {
    uint32_t frequency_khz;   /* 76000..108000 kHz，必须为 10 kHz 的整数倍 */
    uint8_t  rf_cap;          /* 射频电容字段值，范围 0..31 */
} drv_fm_cfg_t;

/* 初始化 FM 模块 */
bool drv_fm_init(const drv_fm_cfg_t *cfg);

/* 手动调频；返回 true 表示已写入配置，不代表已锁台或检测到有效电台。 */
bool drv_fm_tune(uint32_t frequency_khz);

/* 启动 FM 数字通路；DAC/audio buffer 需要由调用方另行初始化。 */
bool drv_fm_start(void);

/* 停止 FM 播放 */
void drv_fm_stop(void);

/* 关闭 FM 模块 */
void drv_fm_power_off(void);

/* 启动一次原始 DMA，直接传递硬件计数。单位及完成标志尚需硬件验证；
 * 不承诺同步完成或输出 PCM 格式，调用方需确认缓冲区大小和生命周期。
 */
bool drv_fm_capture_raw(void *buf, uint32_t transfer_count);

/* 旧接口兼容：samples 原样作为硬件计数传递，不保证对应 int16_t 样本数。 */
bool drv_fm_capture_to_buffer(int16_t *buf, uint32_t samples);

/* enable=true 设置硬件路由位；false 保留原库写 0x444=3 的行为，
 * 未证实它会关闭路由。需停止数字输出时使用 drv_fm_stop()。
 */
bool drv_fm_route_to_audio_buffer(bool enable);

/* 兼容旧接口：当前底层不是同步读接口，函数只启动 DMA 并返回 0。 */
uint32_t drv_fm_get_audio(int16_t *buf, uint32_t max_samples);

/* 同步 DAC / audio buffer 水位；计数应匹配实际音频缓冲配置。
 * SDK RT-Thread 示例传入 total_size_bytes/4，不能直接等同 PCM 样本数。
 */
void drv_fm_sync_dac(uint32_t samples_or_words);
