#include "drv_adc.h"

/* ====== libhal.a 的私有头 ====== */
extern int sdadc_set_sample_rate(uint32_t rate);
extern int sdadc_set_gain(uint16_t gain);
extern int sdadc_set_channel(uint8_t ch);
extern int sdadc_set_dma_samples(uint16_t samples);
extern uint16_t sdadc_get_dma_samples(void);
extern int sdadc_start(void);
extern int sdadc_exit(void);

/* ================================= */

static drv_adc_dma_notice_cb_t adc_dma_notice_cb;

bool drv_adc_init(const drv_adc_cfg_t *cfg)
{
    if (!cfg) return false;

    if (sdadc_set_channel((uint8_t)cfg->channel) != 0) return false;
    if (sdadc_set_sample_rate(cfg->sample_rate) != 0) return false;
    if (sdadc_set_gain(cfg->gain) != 0) return false;

    return true;
}

bool drv_adc_start(void)
{
    return sdadc_start() == 0;
}

void drv_adc_stop(void)
{
    sdadc_exit();
}

bool drv_adc_set_dma_samples(uint16_t samples)
{
    return sdadc_set_dma_samples(samples) == 0;
}

uint16_t drv_adc_get_dma_samples(void)
{
    return sdadc_get_dma_samples();
}

void drv_adc_set_dma_notice_callback(drv_adc_dma_notice_cb_t cb)
{
    adc_dma_notice_cb = cb;
}

void sdadc_dma_notice(uint32_t event)
{
    if (adc_dma_notice_cb) {
        adc_dma_notice_cb(event);
    }
}

uint32_t drv_adc_read(int16_t *buf, uint32_t max_samples)
{
    (void)buf;
    (void)max_samples;
    return 0;
}
