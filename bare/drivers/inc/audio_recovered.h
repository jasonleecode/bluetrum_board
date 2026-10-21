#ifndef AUDIO_RECOVERED_H
#define AUDIO_RECOVERED_H

#include <stdint.h>

/* ABI recovered from libhal.a:audio.o. These are analog input controls,
 * not a DAC, DMA queue or PCM playback API. Channel selectors are encoded
 * independently in bits 3:0 and 7:4; they are not zero-based indices. */
extern const uint8_t mic_anl_table[24];
void set_aux_analog_gain(uint32_t gain, uint32_t channels); /* clamp 0..4 */
void set_mic_analog_gain(uint32_t gain, uint32_t channels); /* clamp 0..23 */
void aux_analog_channel_select(uint32_t channels);
void aux_analog_channel_close(uint32_t channels);
void sdadc_analog_mic_start(uint32_t channels, uint32_t analog_gain);
void sdadc_analog_mic_exit(uint32_t channels);

#endif
