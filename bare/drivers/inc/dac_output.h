#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Native hardware DAC: fixed 48,000 Hz, signed 16-bit stereo PCM.
 * One frame consists of two consecutive int16_t samples (4 bytes). The first
 * sample occupies the low halfword of AUBUFDATA; physical L/R order needs a
 * board test. All calls, including the platform hooks, must be serialized.
 */
bool dac_output_init(void); /* Success leaves PA muted and volume at 55%. */
bool dac_output_active(void);
void dac_output_stop(void); /* Discards queued audio; this is not a drain. */
uint32_t dac_output_write(const int16_t *pcm, uint32_t frame_count);
bool dac_output_set_volume(uint8_t percent); /* Rejects values above 100. */
bool dac_output_mute(bool mute);

struct dac_output_platform {
    uint32_t dma_address;
    void (*delay_ms)(uint32_t milliseconds);
};

/* Weak default returns false, without MMIO. A board override may return true
 * only after reserving PLL1, the DAC and AUBUF0 (no concurrent FM/SDADC/USB
 * audio), confirming the 26 MHz crystal/reference, analog output/VCM wiring,
 * clocks and an empty/reset AUBUF. Map the persistent 1024-byte, 4-byte-aligned
 * buffer to DMA-accessible RAM and provide a real millisecond delay. This
 * implementation accepts only the public BSP's writable RAM window
 * [0x11000, 0x30000), NOT the unverified bare/link.ld 0x20000000 address.
 * This conservative window is not a claim about AUBUF address register width.
 * Perform
 * any required cache maintenance/coherency setup. On false, undo preparation.
 * init() validates the returned mapping's alignment and extent and delay.
 */
bool dac_output_platform_prepare(void *buffer, uint32_t bytes,
                                 struct dac_output_platform *platform);
/* Paired with every successful prepare, including invalid returned settings.
 * Called after digital DAC output and AUBUF interrupts have been disabled;
 * restore board resources here. The generic driver leaves shared PLL/rails on.
 */
void dac_output_platform_release(void);
