/* Recovered from libhal.a:audio.o. See generated/recovery/audio.llvm.txt
 * and audio-differential.json. Preserve each separate volatile RMW, including
 * routes that accumulate bits rather than replacing a previous selection.
 * No board/pin mapping, audio quality or analog settling validation. */
#include "audio_recovered.h"

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define AUDIO_TEXT __attribute__((section(".text")))

const uint8_t mic_anl_table[24] __attribute__((section(".rodata"), aligned(4))) = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
    18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29
};

AUDIO_TEXT void set_aux_analog_gain(uint32_t gain, uint32_t channels)
{
    if (gain > 4) gain = 4;
    if (channels & 15) {
        REG32(0x1f4) &= ~0x00e00000u;
        REG32(0x1f4) |= gain << 21;
    }
    if (channels & 0xf0) {
        REG32(0x1f4) &= ~0x07000000u;
        REG32(0x1f4) |= gain << 24;
    }
}

AUDIO_TEXT void set_mic_analog_gain(uint32_t gain, uint32_t channels)
{
    if (gain > 23) gain = 23;
    gain = mic_anl_table[gain];
    if (channels & 15) {
        REG32(0x1e0) &= ~0x3e0u;
        REG32(0x1e0) |= gain << 5;
    }
    if (channels & 0xf0) {
        REG32(0x1e0) &= ~0x03e00000u;
        REG32(0x1e0) |= gain << 21;
    }
}

AUDIO_TEXT void aux_analog_channel_select(uint32_t channels)
{
    uint32_t low = channels & 15, high = channels & 0xf0;
    switch (low) {
    case 1: case 2: case 3: case 4:
        REG32(0x1f4) |= 1u << (16 + low); break;
    case 5: REG32(0x1f8) |= 0x80; break;
    case 6:
        REG32(0x1f8) |= 0x8000;
        REG32(0x1f8) |= 4;
        REG32(0x1f8) |= 0x200;
        REG32(0x1f8) &= ~0x02000000u;
        set_mic_analog_gain(2, 12); break;
    case 7:
        REG32(0x1f8) |= 0x40000;
        REG32(0x1f8) |= 8;
        REG32(0x1f8) |= 0x800;
        REG32(0x1f8) &= ~0x04000000u;
        set_mic_analog_gain(2, 192); break;
    case 8: REG32(0x1f8) |= 0x2000; break;
    default: break;
    }
    switch (high) {
    case 0x10: case 0x20: case 0x30: case 0x40:
        REG32(0x1f4) |= 1u << (12 + (high >> 4)); break;
    case 0x50: REG32(0x1f8) |= 0x100; break;
    case 0x60:
        REG32(0x1f8) |= 0x8000;
        REG32(0x1f8) |= 4;
        REG32(0x1f8) |= 0x400;
        REG32(0x1f8) &= ~0x02000000u;
        set_mic_analog_gain(2, 12); break;
    case 0x70:
        REG32(0x1f8) |= 0x40000;
        REG32(0x1f8) |= 8;
        REG32(0x1f8) |= 0x1000;
        REG32(0x1f8) &= ~0x04000000u;
        set_mic_analog_gain(2, 192); break;
    case 0x80: REG32(0x1f8) |= 0x4000; break;
    default: break;
    }
}

AUDIO_TEXT void aux_analog_channel_close(uint32_t channels)
{
    uint32_t low = channels & 15, high = channels & 0xf0;
    if (low) {
        REG32(0x1f4) &= 0xffe1ffffu;
        REG32(0x1f8) &= 0xffffd57fu;
        if (low == 6) {
            REG32(0x1f8) &= ~0x8000u;
            REG32(0x1f8) &= ~4u;
            REG32(0x1f8) |= 0x02000000;
        } else if (low == 7) {
            REG32(0x1f8) &= ~0x40000u;
            REG32(0x1f8) &= ~8u;
            REG32(0x1f8) |= 0x04000000;
        }
    }
    if (high) {
        REG32(0x1f4) &= 0xfffe1fffu;
        REG32(0x1f8) &= 0xffffaaffu;
        if (high == 0x60) {
            REG32(0x1f8) &= ~0x8000u;
            REG32(0x1f8) &= ~4u;
            REG32(0x1f8) |= 0x02000000;
        } else if (high == 0x70) {
            REG32(0x1f8) &= ~0x40000u;
            REG32(0x1f8) &= ~8u;
            REG32(0x1f8) |= 0x04000000;
        }
    }
}

AUDIO_TEXT void sdadc_analog_mic_start(uint32_t channels, uint32_t analog_gain)
{
    REG32(0x1f0) |= 0x220b;
    REG32(0x1f8) |= 0x80800000;
    REG32(0x1fc) &= ~0x01000000u;
    REG32(0x1fc) |= 0x02000000;
    REG32(0x1fc) &= ~0x04000000u;
    REG32(0x1fc) |= 0x08000000;
    REG32(0x1fc) &= ~0x10000000u;
    REG32(0x1f8) &= ~0x40u;
    REG32(0x1f8) |= 0x01800000;
    uint32_t low = channels & 15, high = channels & 0xf0;
    if (low) {
        if (low == 12) REG32(0x1f8) |= 0x8000;
        else if (low == 13) REG32(0x1f8) |= 0x20000;
        REG32(0x1f8) |= 4;
        REG32(0x1f8) |= 0x80000;
        REG32(0x1f8) |= 1;
        REG32(0x1f8) &= ~0x02000000u;
    }
    if (high) {
        if (high == 0xc0) REG32(0x1f8) |= 0x40000;
        else if (high == 0xd0) REG32(0x1f8) |= 0x10000;
        REG32(0x1f8) |= 8;
        REG32(0x1f8) |= 0x100000;
        REG32(0x1f8) |= 2;
        REG32(0x1f8) &= ~0x04000000u;
    }
    set_mic_analog_gain(analog_gain, channels);
}

AUDIO_TEXT void sdadc_analog_mic_exit(uint32_t channels)
{
    if (channels & 15) {
        REG32(0x1f8) &= ~0x28000u;
        REG32(0x1f8) &= ~4u;
        REG32(0x1f8) &= ~0x80000u;
        REG32(0x1f8) &= ~1u;
        REG32(0x1f8) |= 0x02000000;
    }
    if (channels & 0xf0) {
        REG32(0x1f8) &= ~0x50000u;
        REG32(0x1f8) &= ~8u;
        REG32(0x1f8) &= ~0x100000u;
        REG32(0x1f8) &= ~2u;
        REG32(0x1f8) |= 0x04000000;
    }
}
