/* Reconstructed from libhal.a:sdadc.o. Raw ABI preserves vendor behavior;
 * use drv_adc for validated DMA configuration. See AUDIO_RECOVERY.md. */
#include "sdadc_recovered.h"
#include <stddef.h>

#define REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))

/* Original _cfg is 12 zero-initialized bytes; only bytes 0..5 are accessed. */
static _Alignas(4) struct {
    uint8_t channel;
    uint8_t rate_index;
    uint16_t gain;
    uint16_t dma_samples;
    uint8_t unknown[6];
} cfg __attribute__((section(".bss")));
_Static_assert(offsetof(__typeof__(cfg), dma_samples) == 4, "observed layout");
_Static_assert(sizeof(cfg) == 12, "observed size");

extern void hal_printf(const char *format, ...);
extern void hal_mdelay(uint32_t milliseconds);
extern void sdadc_analog_mic_start(uint32_t channel, uint32_t analog_gain);
extern void sdadc_analog_aux_start(uint32_t channel, uint32_t analog_gain);
extern void sdadc_analog_mic_exit(uint32_t channel);
extern void sdadc_analog_aux_exit(uint32_t channel);

/* .text+0x000: zero rejected; this is an encoded mask, not a 0-based enum. */
__attribute__((section(".text"))) int sdadc_set_channel(uint8_t channel)
{
    if (!channel) return -1;
    cfg.channel = channel;
    return 0;
}

/* .text+0x012; table bytes recovered from sdadc.o:.rodata+0. */
__attribute__((section(".text"))) int sdadc_set_sample_rate(uint32_t rate)
{
    static const uint8_t divider[] = {0, 0, 0, 1, 2, 2, 3, 4, 4, 5};
    uint8_t index;
    switch (rate) {
    case 48000: index = 0; break;
    case 44100: index = 1; break;
    case 38000: index = 2; break; /* Literal 0x9470, NOT 32000 or 38400. */
    case 32000: index = 3; break;
    case 24000: index = 4; break;
    case 22050: index = 5; break;
    case 16000: index = 6; break;
    case 12000: index = 7; break;
    case 11025: index = 8; break;
    case  8000: index = 9; break;
    default:
        hal_printf("unsupport sample rate: %d\n", (int)rate);
        return -1;
    }
    cfg.rate_index = index; /* Stored even when channel is zero. */
    if (!cfg.channel) return -1;

    if ((index & 11u) == 1u || index == 8u) {
        uint32_t clock = REG32(0x3a8);
        if (REG32(0x140) & 2u) {
            REG32(0x3a8) = (clock & ~0xf0u) | 0xa0u;
            REG32(0x390) = 0x98d80;
            REG32(0x39c) |= 0x100000;
            hal_mdelay(1);
            REG32(0x140) &= ~2u;
        }
    } else {
        uint32_t clock = REG32(0x3a8);
        if (!(REG32(0x140) & 2u)) {
            REG32(0x3a8) = (clock & ~0xf0u) | 0x90u;
            REG32(0x390) = 0x973ca;
            REG32(0x39c) |= 0x100000;
            hal_mdelay(1);
            REG32(0x140) |= 2u;
        }
    }
    REG32(0x1dc) &= ~0x38u;
    REG32(0x1dc) |= (uint32_t)divider[index] << 3;
    /* Original also branches on index bit 7 for bits 11..13. All ten
     * accepted rates overwrite the entire byte with 0..9, so it is dead
     * on this entry path. No write to that bit is present in sdadc.o. */
    return 0;
}

/* .text+0x1de */
__attribute__((section(".text"))) int sdadc_set_gain(uint16_t gain)
{
    cfg.gain = gain;
    if (!cfg.channel) return -1;
    REG32(0x1e0) &= ~31u;
    REG32(0x1e0) |= gain & 31u;
    return 0;
}

/* .text+0x20e: no range validation, including zero. */
__attribute__((section(".text"))) int sdadc_set_dma_samples(uint16_t samples)
{
    cfg.dma_samples = samples;
    return 0;
}

__attribute__((section(".text"))) uint8_t sdadc_get_channel(void) { return cfg.channel; }       /* +0x21a */
__attribute__((section(".text"))) uint8_t sdadc_get_sample_rate(void) { return cfg.rate_index; } /* +0x224: NOT Hz */
__attribute__((section(".text"))) uint16_t sdadc_get_gain(void) { return cfg.gain; }             /* +0x22e */
__attribute__((section(".text"))) uint16_t sdadc_get_dma_samples(void) { return cfg.dma_samples; } /* +0x238 */

/* .text+0x242: incoming a0 goes to 0x1cc or 0x1d4 before any call.
 * Pointer type is inferred; the machine-level fact is a 32-bit DMA address.
 * Both-bank mode writes only 0x1cc in this function, exactly as the object.
 */
__attribute__((section(".text"))) int sdadc_start(void *dma_buffer)
{
    uint32_t channel = cfg.channel;
    uint32_t low = channel & 15u, high = channel & 0xf0u;
    uint32_t analog_gain = (cfg.gain >> 5) & 255u;
    if (!channel) return -1;
    if (low) {
        REG32(0x1cc) = (uint32_t)(uintptr_t)dma_buffer;
        REG32(0x1d0) = (uint32_t)cfg.dma_samples - 1u;
        if (high) {
            REG32(0x1c0) = 0;
            REG32(0x1dc) |= 0x202;
            REG32(0x1dc) |= 0x404;
        } else {
            REG32(0x1c0) &= ~0x35u;
            REG32(0x1dc) |= 2;
            REG32(0x1dc) |= 4;
        }
        if ((uint32_t)(low - 12u) <= 1u)
            sdadc_analog_mic_start(channel, analog_gain);
        else
            sdadc_analog_aux_start(channel, analog_gain);
        REG32(0x1c0) |= high ? 3u : 1u;
        REG32(0x1c0) |= 0x30;
        REG32(0x1dc) |= high ? 0x101u : 1u;
    } else {
        REG32(0x1d4) = (uint32_t)(uintptr_t)dma_buffer;
        REG32(0x1d8) = (uint32_t)cfg.dma_samples - 1u;
        REG32(0x1c0) &= ~0xc6u;
        REG32(0x1dc) |= 0x200;
        REG32(0x1dc) |= 0x400;
        if ((channel & 0xe0u) == 0xc0u)
            sdadc_analog_mic_start(high, analog_gain);
        else
            sdadc_analog_aux_start(high, analog_gain);
        REG32(0x1c0) |= 2;
        REG32(0x1c0) |= 0xc0;
        REG32(0x1dc) |= 0x100;
    }
    return 0;
}

/* .text+0x3be: stop paths also clear the selected channel configuration. */
__attribute__((section(".text"))) int sdadc_exit(void)
{
    uint32_t channel = cfg.channel;
    uint32_t low = channel & 15u, high = channel & 0xf0u;
    if (!channel) return -1;
    if (low) {
        uint32_t selected = high ? channel : low;
        if ((uint32_t)(low - 12u) <= 1u)
            sdadc_analog_mic_exit(selected);
        else
            sdadc_analog_aux_exit(selected);
        REG32(0x1c0) &= high ? ~3u : ~1u;
        REG32(0x1c0) &= ~0x30u;
        REG32(0x1dc) &= high ? ~0x101u : ~1u;
        cfg.channel = high ? 0 : cfg.channel & 0xf0u;
    } else {
        if ((channel & 0xe0u) == 0xc0u)
            sdadc_analog_mic_exit(high);
        else
            sdadc_analog_aux_exit(high);
        REG32(0x1c0) &= ~2u;
        REG32(0x1c0) &= ~0xc0u;
        REG32(0x1dc) &= ~0x100u;
        cfg.channel &= 15u;
    }
    if (!cfg.channel) {
        REG32(0x1c0) = 0;
        REG32(0x1dc) = 0;
        REG32(0x1f8) &= 0x7e7fffbf;
    }
    return 0;
}

/* .irq.sdadc+0: vendor default weak callback is a single ret. */
__attribute__((weak, section(".irq.sdadc"))) void sdadc_dma_notice(uint32_t event) { (void)event; }

/* .irq.sdadc+0x2: preserve fresh status reads and if/else priority.
 * Event numbers are observed; half/full-buffer semantics are NOT confirmed.
 */
__attribute__((weak, section(".irq.sdadc"))) void sdadc_isr(void)
{
    if (REG32(0x1c8) & 2u) {
        REG32(0x1c4) = 2;
        sdadc_dma_notice(1);
    } else if (REG32(0x1c8) & 1u) {
        REG32(0x1c4) = 1;
        sdadc_dma_notice(2);
    }
    if (REG32(0x1c8) & 8u) {
        REG32(0x1c4) = 8;
        sdadc_dma_notice(3);
    } else if (REG32(0x1c8) & 4u) {
        REG32(0x1c4) = 4;
        sdadc_dma_notice(4);
    }
}
