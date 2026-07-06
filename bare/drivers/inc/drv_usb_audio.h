#pragma once
#include <stdint.h>
#include <stdbool.h>

void drv_usb_audio_init(void);
void drv_usb_audio_set_volume(uint8_t vol);
void drv_usb_audio_mute(bool enable);
bool drv_usb_audio_enable_fm_dma(bool enable);
bool drv_usb_audio_send(int16_t *buf, uint32_t len);
