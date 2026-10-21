#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    DRV_ADC_CH_MIC = 0, /* mapped to vendor channel 0x0c, single DMA bank */
    DRV_ADC_CH_AUX,     /* unavailable: vendor AUX start/exit glue is missing */
} drv_adc_channel_t;

typedef struct {
    uint32_t sample_rate;
    uint16_t gain; /* digital bits 0..4; analog index bits 5..12 (0..23) */
    drv_adc_channel_t channel;
} drv_adc_cfg_t;

typedef void (*drv_adc_dma_notice_cb_t)(uint32_t event);
/* Platform must own the shared audio clock/analog resources and provide real
 * delay and IRQ routing. DMA validator checks physical address, extent, count
 * units, layout and cache visibility; a byte capacity alone proves none of
 * these. Default hooks reject init/DMA. No allocation or implicit mapping.
 * Serialize all configuration/start/stop calls; ISR callbacks must not reenter
 * these APIs. A platform stop must quiesce the IRQ before storage is reused. */
bool drv_adc_platform_init(const drv_adc_cfg_t *cfg);
bool drv_adc_platform_dma(void *buffer, size_t capacity_bytes, uint16_t dma_units,
                          uint32_t *bus_address);
void drv_adc_platform_stop(void);

bool drv_adc_init(const drv_adc_cfg_t *cfg);
/* Start caller-owned DMA storage. Retain it until stop has disabled DMA.
 * Notifications are raw vendor event IDs, not guaranteed half/full markers. */
bool drv_adc_start_dma(void *buffer, size_t capacity_bytes);
/* Legacy no-buffer entry cannot safely start DMA and returns false. */
bool drv_adc_start(void);
void drv_adc_stop(void); /* stop invalidates configuration; re-init to restart */
bool drv_adc_set_dma_samples(uint16_t dma_units);
uint16_t drv_adc_get_dma_samples(void);
void drv_adc_set_dma_notice_callback(drv_adc_dma_notice_cb_t cb);
void drv_adc_isr(void); /* platform SDADC vector calls this; ISR callback context */
/* No verified PCM frame/layout mapping yet; synchronous read remains absent. */
uint32_t drv_adc_read(int16_t *buf, uint32_t max_samples);
