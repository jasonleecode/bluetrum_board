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

typedef void (*drv_adc_dma_notice_cb_t)(uint32_t event);

/* 初始化 ADC */
bool drv_adc_init(const drv_adc_cfg_t *cfg);

/* 启动采样 */
bool drv_adc_start(void);

/* 停止采样 */
void drv_adc_stop(void);

/* 设置/查询底层 DMA 采样点数配置 */
bool drv_adc_set_dma_samples(uint16_t samples);
uint16_t drv_adc_get_dma_samples(void);

/* 注册 SDADC DMA 中断通知回调。event 由底层 sdadc_isr 传入。 */
void drv_adc_set_dma_notice_callback(drv_adc_dma_notice_cb_t cb);

/* 兼容旧接口：当前 libhal 符号不支持同步读 buffer，函数固定返回 0。 */
uint32_t drv_adc_read(int16_t *buf, uint32_t max_samples);
