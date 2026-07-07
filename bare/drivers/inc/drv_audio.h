#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    DRV_AUDIO_ROUTE_NONE = 0,
    DRV_AUDIO_ROUTE_FM_TO_BUFFER,
    DRV_AUDIO_ROUTE_USB_AUDIO,
} drv_audio_route_t;

bool drv_audio_init(void);
bool drv_audio_set_route(drv_audio_route_t route);
void drv_audio_set_volume(uint8_t volume);
void drv_audio_mute(bool enable);
bool drv_audio_write(const int16_t *samples, uint32_t sample_count);
