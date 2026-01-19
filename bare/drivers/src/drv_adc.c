#include "drv_adc.h"

/* ====== libhal.a 的私有头 ====== */
extern void sdadc_set_sample_rate(uint32_t rate);
extern void sdadc_set_gain(uint8_t gain);
extern void sdadc_set_channel(uint8_t ch);
extern void sdadc_start(void);
extern void sdadc_exit(void);
extern uint32_t sdadc_get_dma_samples(int16_t *buf, uint32_t max);

/* ================================= */

bool drv_adc_init(const drv_adc_cfg_t *cfg)
{
    if (!cfg) return false;

    sdadc_set_sample_rate(cfg->sample_rate);
    sdadc_set_gain(cfg->gain);
    sdadc_set_channel((uint8_t)cfg->channel);

    return true;
}

bool drv_adc_start(void)
{
    sdadc_start();
    return true;
}

void drv_adc_stop(void)
{
    sdadc_exit();
}

uint32_t drv_adc_read(int16_t *buf, uint32_t max_samples)
{
    return sdadc_get_dma_samples(buf, max_samples);
}