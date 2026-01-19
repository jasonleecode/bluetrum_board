#include "drv_usb_audio.h"
#include <stdint.h>

/* libhal.a 符号 */
extern void uda_init(void);
extern void uda_run_loop_execute(void);
extern void uda_set_spk_volume(uint8_t vol);
extern void uda_set_spk_mute(uint8_t en);
extern void usb_isoc_reset(void);
extern void fmrx_dma_to_aubuf(void *buf, uint32_t len);

void drv_usb_audio_init(void)
{
    uda_init();
}

void drv_usb_audio_set_volume(uint8_t vol)
{
    uda_set_spk_volume(vol);
}

void drv_usb_audio_mute(uint8_t enable)
{
    uda_set_spk_mute(enable ? 1 : 0);
}

/* 输出数据到 USB（简单封装） */
void drv_usb_audio_send(int16_t *buf, uint32_t len)
{
    /* 假设底层 HAL 已有 DMA */
    fmrx_dma_to_aubuf(buf, len);
}