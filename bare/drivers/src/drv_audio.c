#include "drv_audio.h"
#include "drv_usb_audio.h"

extern void fmrx_dma_to_aubuf(uint32_t enable);

static drv_audio_route_t audio_route;

bool drv_audio_init(void)
{
    drv_usb_audio_init();
    audio_route = DRV_AUDIO_ROUTE_USB_AUDIO;
    return true;
}

bool drv_audio_set_route(drv_audio_route_t route)
{
    switch (route) {
    case DRV_AUDIO_ROUTE_NONE:
        fmrx_dma_to_aubuf(0);
        audio_route = route;
        return true;
    case DRV_AUDIO_ROUTE_FM_TO_BUFFER:
        fmrx_dma_to_aubuf(1);
        audio_route = route;
        return true;
    case DRV_AUDIO_ROUTE_USB_AUDIO:
        audio_route = route;
        return true;
    default:
        return false;
    }
}

void drv_audio_set_volume(uint8_t volume)
{
    drv_usb_audio_set_volume(volume);
}

void drv_audio_mute(bool enable)
{
    drv_usb_audio_mute(enable);
}

bool drv_audio_write(const int16_t *samples, uint32_t sample_count)
{
    if (audio_route != DRV_AUDIO_ROUTE_USB_AUDIO || !samples || sample_count == 0) {
        return false;
    }

    return drv_usb_audio_send((int16_t *)samples, sample_count);
}
