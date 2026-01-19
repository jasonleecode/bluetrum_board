#include "drv_fm.h"

/* libhal.a 符号 */
extern void fmrx_analog_init(void);
extern void fmrx_set_pll(uint32_t freq_khz);
extern void fmrx_set_rf_cap(uint8_t val);
extern void fmrx_power_on(void);
extern void fmrx_power_off(void);
extern void fmrx_digital_start(void);
extern void fmrx_digital_stop(void);
extern void fmrx_dac_sync(void);
extern uint32_t fmrx_dma_to_aubuf(void *buf, uint32_t len);
extern uint32_t fmrx_get_audio_data(int16_t *buf, uint32_t len);

/* 内部状态 */
static bool fm_initialized = false;

bool drv_fm_init(const drv_fm_cfg_t *cfg)
{
    if (!cfg) return false;

    /* 初始化模拟前端 */
    fmrx_analog_init();

    /* 设置频率 PLL */
    fmrx_set_pll(cfg->frequency_khz);

    /* 校准射频电容 */
    fmrx_set_rf_cap(cfg->rf_cap);

    /* 打开功放 / FM 模块 */
    fmrx_power_on();

    fm_initialized = true;
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

uint32_t drv_fm_get_audio(int16_t *buf, uint32_t max_samples)
{
    if (!fm_initialized || !buf) return 0;

    /* 从 DMA buffer 读取音频数据 */
    return fmrx_get_audio_data(buf, max_samples);
}

void drv_fm_sync_dac(void)
{
    if (!fm_initialized) return;

    fmrx_dac_sync();
}