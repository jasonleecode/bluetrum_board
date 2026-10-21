#include "drv_fm.h"
#include "fmrx_hal.h"

/* 内部状态 */
static bool fm_initialized = false;

static bool frequency_valid(uint32_t frequency_khz)
{
    /* Datasheet band; the recovered PLL ABI uses 10-kHz units. */
    return frequency_khz >= 76000u && frequency_khz <= 108000u
        && frequency_khz % 10u == 0u;
}

bool drv_fm_init(const drv_fm_cfg_t *cfg)
{
    if (!cfg || !frequency_valid(cfg->frequency_khz) || cfg->rf_cap > 31u) return false;

    /* fmrx_power_on 内部会初始化模拟前端。 */
    fmrx_power_on(0);

    /* 设置频率 PLL */
    fmrx_set_pll(1, cfg->frequency_khz / 10u);

    /* 校准射频电容 */
    fmrx_set_rf_cap(cfg->rf_cap);

    fm_initialized = true;
    return true;
}

bool drv_fm_tune(uint32_t frequency_khz)
{
    if (!fm_initialized || !frequency_valid(frequency_khz)) return false;
    fmrx_set_pll(1, frequency_khz / 10u);
    return true;
}

bool drv_fm_start(void)
{
    if (!fm_initialized) return false;

    /* 启动数字输出 */
    fmrx_digital_start();
    return true;
}

void drv_fm_stop(void)
{
    if (!fm_initialized) return;

    /* 停止数字输出 */
    fmrx_digital_stop();
}

void drv_fm_power_off(void)
{
    if (!fm_initialized) return;

    fmrx_power_off();
    fm_initialized = false;
}

bool drv_fm_capture_to_buffer(int16_t *buf, uint32_t samples)
{
    return drv_fm_capture_raw(buf, samples);
}

bool drv_fm_capture_raw(void *buf, uint32_t transfer_count)
{
    if (!fm_initialized || !buf || transfer_count == 0) return false;
    fmrx_get_audio_data(buf, transfer_count);
    return true;
}

bool drv_fm_route_to_audio_buffer(bool enable)
{
    if (!fm_initialized) return false;

    fmrx_dma_to_aubuf(enable ? 1u : 0u);
    return true;
}

uint32_t drv_fm_get_audio(int16_t *buf, uint32_t max_samples)
{
    (void)drv_fm_capture_to_buffer(buf, max_samples);
    return 0;
}

void drv_fm_sync_dac(uint32_t samples_or_words)
{
    if (!fm_initialized) return;

    fmrx_dac_sync(samples_or_words);
}
