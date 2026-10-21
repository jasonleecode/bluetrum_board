#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    DRV_AUDIO_ROUTE_NONE = 0,
    DRV_AUDIO_ROUTE_FM_TO_BUFFER,
    DRV_AUDIO_ROUTE_USB_AUDIO,
    DRV_AUDIO_ROUTE_DAC,
} drv_audio_route_t;

/* Native DAC, 48 kHz signed 16-bit stereo. Needs dac_output_platform_prepare;
 * default board scaffold rejects init. Success starts muted; unmute explicitly.
 * This owns PLL1/AUBUF and must not run concurrently with FM/SDADC/USB audio. */
bool drv_audio_init(void);
void drv_audio_stop(void); /* discards queued playback */
bool drv_audio_set_route(drv_audio_route_t route);
void drv_audio_set_volume(uint8_t volume); /* 0..100; invalid values ignored */
void drv_audio_mute(bool enable);
/* Nonblocking; returns frames accepted (two int16_t samples per frame).
 * Caller can reuse accepted input immediately; retry only the remaining tail.
 * Samples have been queued, not necessarily played at the analog output. */
uint32_t drv_audio_write_frames(const int16_t *samples, uint32_t frame_count);
/* Legacy USB-only all-or-nothing API, currently unsupported. DAC callers must
 * use write_frames to account for partial writes without replaying samples. */
bool drv_audio_write(const int16_t *samples, uint32_t sample_count);
