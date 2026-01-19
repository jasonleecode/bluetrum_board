#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    DRV_ADC_CH_MIC = 0,
    DRV_ADC_CH_AUX,
} drv_adc_channel_t;

typedef struct {
    uint32_t sample_rate;
    uint8_t  gain;
    drv_adc_channel_t channel;
} drv_adc_cfg_t;

/* 初始化 ADC */
bool drv_adc_init(const drv_adc_cfg_t *cfg);

/* 启动采样 */
bool drv_adc_start(void);

/* 停止采样 */
void drv_adc_stop(void);

/* 读取 DMA 采样数据 */
uint32_t drv_adc_read(int16_t *buf, uint32_t max_samples);