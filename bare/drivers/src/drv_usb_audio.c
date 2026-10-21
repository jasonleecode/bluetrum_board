#include "drv_usb_audio.h"
#include <stdint.h>
#include "fmrx_hal.h"

bool drv_usb_audio_init(void)
{
    /* uda_init requires a bound context/EP2 OUT receive buffer. The old
     * no-argument call was invalid; no validated audio device port exists. */
    return false;
}

void drv_usb_audio_set_volume(uint8_t vol)
{
    (void)vol;
}

void drv_usb_audio_mute(bool enable)
{
    (void)enable;
}

bool drv_usb_audio_enable_fm_dma(bool enable)
{
    fmrx_dma_to_aubuf(enable ? 1u : 0u);
    return true;
}

bool drv_usb_audio_send(int16_t *buf, uint32_t len)
{
    /* EP2 is an OUT receive endpoint. It is not an audio-upload function,
     * and passing endpoint number 2 as a pointer dereferenced address 2. */
    (void)buf;
    (void)len;
    return false;
}
