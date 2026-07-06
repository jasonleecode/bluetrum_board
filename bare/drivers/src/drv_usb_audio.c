#include "drv_usb_audio.h"
#include <stdint.h>

/* libhal.a 符号 */
extern void uda_init(void);
extern void uda_run_loop_execute(void);
extern void uda_set_spk_volume(uint8_t vol);
extern void uda_set_spk_mute(uint8_t en);
extern void usb_isoc_reset(void);
extern void fmrx_dma_to_aubuf(uint32_t enable);

void drv_usb_audio_init(void)
{
    uda_init();
}

void drv_usb_audio_set_volume(uint8_t vol)
{
    uda_set_spk_volume(vol);
}

void drv_usb_audio_mute(bool enable)
{
    uda_set_spk_mute(enable ? 1 : 0);
}

bool drv_usb_audio_enable_fm_dma(bool enable)
{
    fmrx_dma_to_aubuf(enable ? 1u : 0u);
    return true;
}

bool drv_usb_audio_send(int16_t *buf, uint32_t len)
{
    (void)buf;
    (void)len;
    return false;
}
