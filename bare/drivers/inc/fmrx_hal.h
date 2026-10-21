#pragma once
#include <stdint.h>

/* ABI recovered from libhal.a:fmrx.o and fmrx_ang.o.
 * See docs/lib-analysis/FM_RECOVERY.md for evidence and hardware limitations.
 */
void fmrx_power_on(uint32_t val); /* SDK callers pass zero; this object ignores it. */
void fmrx_power_off(void);
void fmrx_digital_start(void);
void fmrx_digital_stop(void);
void fmrx_dac_sync(uint32_t buffer_count);
/* Integration helper, not an original SDK symbol: neutralize sync correction. */
void fmrx_dac_sync_reset(void);
void fmrx_dma_to_aubuf(uint8_t enable);
void fmrx_get_audio_data(void *buffer, uint32_t transfer_count);

void fmrx_analog_init(void);
/* analog_init uses mode=1 and frequency_10khz=8750 (87.5 MHz).
 * Keep mode opaque: its full RF meaning has not been established.
 */
void fmrx_set_pll(uint32_t mode, uint32_t frequency_10khz);
void fmrx_set_rf_cap(uint32_t value);
