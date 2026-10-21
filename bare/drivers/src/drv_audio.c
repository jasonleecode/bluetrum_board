#include "drv_audio.h"
#include "drv_usb_audio.h"
#include "dac_output.h"
#include "fmrx_hal.h"

static drv_audio_route_t audio_route;

bool drv_audio_init(void)
{
    if (audio_route != DRV_AUDIO_ROUTE_NONE || !dac_output_init()) return false;
    audio_route = DRV_AUDIO_ROUTE_DAC;
    return true;
}

void drv_audio_stop(void)
{
    if (audio_route == DRV_AUDIO_ROUTE_FM_TO_BUFFER) fmrx_dma_to_aubuf(0);
    if (audio_route == DRV_AUDIO_ROUTE_DAC) dac_output_stop();
    audio_route = DRV_AUDIO_ROUTE_NONE;
}

bool drv_audio_set_route(drv_audio_route_t route)
{
    if (route == DRV_AUDIO_ROUTE_NONE) {
        drv_audio_stop();
        return true;
    }
    if (route == audio_route) return true;
    /* Clock/buffer ownership must be released explicitly before switching. */
    if (audio_route != DRV_AUDIO_ROUTE_NONE || dac_output_active()) return false;
    switch (route) {
    case DRV_AUDIO_ROUTE_DAC:
        return drv_audio_init();
    case DRV_AUDIO_ROUTE_FM_TO_BUFFER:
        fmrx_dma_to_aubuf(1);
        audio_route = route;
        return true;
    case DRV_AUDIO_ROUTE_USB_AUDIO:
        if (!drv_usb_audio_init()) return false;
        audio_route = route;
        return true;
    default:
        return false;
    }
}

void drv_audio_set_volume(uint8_t volume)
{
    if (audio_route == DRV_AUDIO_ROUTE_DAC) (void)dac_output_set_volume(volume);
    else if (audio_route == DRV_AUDIO_ROUTE_USB_AUDIO) drv_usb_audio_set_volume(volume);
}

void drv_audio_mute(bool enable)
{
    if (audio_route == DRV_AUDIO_ROUTE_DAC) (void)dac_output_mute(enable);
    else if (audio_route == DRV_AUDIO_ROUTE_USB_AUDIO) drv_usb_audio_mute(enable);
}

uint32_t drv_audio_write_frames(const int16_t *samples, uint32_t frames)
{
    if (audio_route != DRV_AUDIO_ROUTE_DAC) return 0;
    return dac_output_write(samples, frames);
}

bool drv_audio_write(const int16_t *samples, uint32_t sample_count)
{
    if (audio_route != DRV_AUDIO_ROUTE_USB_AUDIO || !samples || !sample_count) return false;
    return drv_usb_audio_send((int16_t *)samples, sample_count);
}
