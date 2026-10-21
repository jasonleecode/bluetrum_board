#include "drv_adc.h"
#include "sdadc_recovered.h"

static drv_adc_dma_notice_cb_t adc_dma_notice_cb;
static bool initialized;
static volatile bool running; /* observed by the real SDADC ISR */
static uint16_t dma_units;

__attribute__((weak)) bool drv_adc_platform_init(const drv_adc_cfg_t *cfg)
{
    (void)cfg;
    return false;
}
__attribute__((weak)) bool drv_adc_platform_dma(void *buffer, size_t bytes,
                                               uint16_t units, uint32_t *address)
{
    (void)buffer; (void)bytes; (void)units; (void)address;
    return false;
}
__attribute__((weak)) void drv_adc_platform_stop(void) {}

static bool valid_rate(uint32_t rate)
{
    switch (rate) {
    case 48000: case 44100: case 38000: case 32000: case 24000:
    case 22050: case 16000: case 12000: case 11025: case 8000: return true;
    default: return false;
    }
}

bool drv_adc_init(const drv_adc_cfg_t *cfg)
{
    if (!cfg || initialized || cfg->channel != DRV_ADC_CH_MIC ||
        !valid_rate(cfg->sample_rate) || (cfg->gain >> 5) >= 24)
        return false;
    if (!drv_adc_platform_init(cfg)) return false;
    /* Vendor zero is invalid. Restrict to the established MIC path and one
     * DMA bank; the vendor dual-bank start does not program both addresses. */
    if (sdadc_set_channel(0x0c) || sdadc_set_sample_rate(cfg->sample_rate) ||
        sdadc_set_gain(cfg->gain)) {
        sdadc_exit();
        drv_adc_platform_stop();
        return false;
    }
    initialized = true;
    dma_units = 0;
    return true;
}

bool drv_adc_start_dma(void *buffer, size_t capacity_bytes)
{
    uint32_t address = 0;
    if (!initialized || running || !buffer || !capacity_bytes ||
        ((uintptr_t)buffer & 3u) || !dma_units)
        return false;
    if (!drv_adc_platform_dma(buffer, capacity_bytes, dma_units, &address) ||
        !address || (address & 3u)) return false;
    /* Set before enabling the peripheral so a real immediate IRQ is visible. */
    running = true;
    if (sdadc_start((void *)(uintptr_t)address) != 0) {
        running = false;
        return false;
    }
    return true;
}

bool drv_adc_start(void) { return false; }

void drv_adc_stop(void)
{
    if (!initialized) return;
    running = false;
    sdadc_exit();
    initialized = false;
    dma_units = 0;
    drv_adc_platform_stop();
}

bool drv_adc_set_dma_samples(uint16_t units)
{
    if (!initialized || running || !units) return false;
    if (sdadc_set_dma_samples(units) != 0) return false;
    dma_units = units;
    return true;
}

uint16_t drv_adc_get_dma_samples(void) { return dma_units; }
void drv_adc_set_dma_notice_callback(drv_adc_dma_notice_cb_t cb) { adc_dma_notice_cb = cb; }

void sdadc_dma_notice(uint32_t event)
{
    if (running && adc_dma_notice_cb) adc_dma_notice_cb(event);
}
void drv_adc_isr(void) { if (running) sdadc_isr(); }

uint32_t drv_adc_read(int16_t *buf, uint32_t max_samples)
{
    (void)buf; (void)max_samples;
    return 0;
}
