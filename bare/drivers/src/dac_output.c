/* Copyright (c) 2020-2021, Bluetrum Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * Native DAC/AUBUF implementation based on the public Bluetrum RT-Thread BSP:
 * rtt/bsp/board/ports/audio/drv_sound.c (Apache-2.0, Bluetrum 2020-2021).
 * This is a bounded polling wrapper, not a recovered libhal entry point.
 */
#include "dac_output.h"
#include <stddef.h>

#ifdef DAC_OUTPUT_TEST
extern uint32_t dac_output_test_read(uint32_t address);
extern void dac_output_test_write(uint32_t address, uint32_t value);
#define READ(a) dac_output_test_read(a)
#define WRITE(a, v) dac_output_test_write((a), (v))
#else
#define READ(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define WRITE(a, v) (*(volatile uint32_t *)(uintptr_t)(a) = (v))
#endif
#define BIT(n) (UINT32_C(1) << (n))
enum {
    AUBUFDATA = 0x104, AUBUFCON = 0x108, AUBUFSTARTADDR = 0x10c,
    AUBUFSIZE = 0x110, DACDIGCON0 = 0x140, DACVOLCON = 0x144,
    AUANGCON0 = 0x1f0, AUANGCON1 = 0x1f4, AUANGCON3 = 0x1fc,
    PLL1DIV = 0x390, PLL1CON = 0x39c, CLKCON2 = 0x3a8,
    DMA_BYTES = 1024, DMA_RAM_BEGIN = 0x11000, DMA_RAM_END = 0x30000
};
static _Alignas(4) uint32_t dma_buffer[DMA_BYTES / sizeof(uint32_t)];
static bool active;

static void set_bits(uint32_t address, uint32_t bits)
{
    WRITE(address, READ(address) | bits);
}
static void clear_bits(uint32_t address, uint32_t bits)
{
    WRITE(address, READ(address) & ~bits);
}

__attribute__((weak)) bool dac_output_platform_prepare(
    void *buffer, uint32_t bytes, struct dac_output_platform *platform)
{
    (void)buffer; (void)bytes; (void)platform;
    return false;
}
__attribute__((weak)) void dac_output_platform_release(void) {}

bool dac_output_init(void)
{
    struct dac_output_platform platform = {0};
    if (active) return false;
    /* Initialize RAM before the board hook sets up any DMA coherency. */
    for (uint32_t i = 0; i < DMA_BYTES / sizeof(uint32_t); ++i)
        dma_buffer[i] = 0;
    if (!dac_output_platform_prepare(dma_buffer, DMA_BYTES, &platform))
        return false;
    if (!platform.delay_ms || (platform.dma_address & 3u) ||
        platform.dma_address < DMA_RAM_BEGIN ||
        platform.dma_address > DMA_RAM_END - DMA_BYTES) {
        dac_output_platform_release();
        return false;
    }

    /* Fixed 48 kHz family, 26 MHz reference, 245.76 MHz PLL / 10 / 512.
     * Use the integer truncation of the BSP's floating point constant. */
    clear_bits(PLL1CON, BIT(16) | BIT(17));
    clear_bits(CLKCON2, 0xf0u);
    clear_bits(PLL1CON, BIT(3) | BIT(4) | BIT(5));
    set_bits(PLL1CON, BIT(3));
    set_bits(PLL1CON, BIT(12));
    platform.delay_ms(1);
    set_bits(PLL1CON, BIT(18));
    set_bits(CLKCON2, BIT(4) | BIT(7));
    WRITE(PLL1DIV, UINT32_C(0x973ca));
    platform.delay_ms(1);
    set_bits(PLL1CON, BIT(20));
    set_bits(PLL1CON, BIT(6));
    platform.delay_ms(1);
    /* sdadc.o uses this bit for the shared 48 kHz / 44.1 kHz PLL family.
     * Keep it consistent with the divider just programmed. */
    set_bits(DACDIGCON0, BIT(1));

    set_bits(AUANGCON1, BIT(3)); /* Keep PA muted throughout startup. */
    set_bits(AUANGCON0, BIT(0) | BIT(1) | BIT(3));
    clear_bits(AUANGCON0, BIT(6) | BIT(5) | BIT(4));
    set_bits(AUANGCON0, 3u << 4);
    set_bits(AUANGCON0, BIT(2));
    set_bits(AUANGCON0, BIT(9));
    clear_bits(AUANGCON0, BIT(13) | BIT(12));
    set_bits(AUANGCON0, 2u << 12);
    set_bits(AUANGCON0, BIT(15) | BIT(16) | BIT(17) | BIT(18));
    clear_bits(AUANGCON0, BIT(11)); /* External VCM capacitor. */
    clear_bits(AUANGCON0, BIT(19)); /* Switched-capacitor DAC. */
    set_bits(AUANGCON0, BIT(20));   /* 3.3 V single-ended PA. */
    clear_bits(AUANGCON3, 0x7f7fu);
    set_bits(AUANGCON1, BIT(0) | BIT(1) | BIT(12));
    clear_bits(AUANGCON1, BIT(29));
    clear_bits(DACDIGCON0, (0xfu << 2) | BIT(7) | BIT(8));
    set_bits(DACDIGCON0, BIT(6));
    WRITE(DACVOLCON, 55u * 327u | (2u << 16));

    WRITE(AUBUFSIZE, (DMA_BYTES / 4u - 1u) | ((DMA_BYTES / 8u) << 16));
    WRITE(AUBUFSTARTADDR, platform.dma_address);
    /* Polling owns AUBUF0; don't depend on RT-Thread's interrupt/semaphore. */
    clear_bits(AUBUFCON, BIT(4));
    set_bits(DACDIGCON0, BIT(0) | BIT(10));
    set_bits(AUBUFCON, BIT(1));
    active = true;
    return true;
}

bool dac_output_active(void) { return active; }

void dac_output_stop(void)
{
    if (!active) return;
    set_bits(AUANGCON1, BIT(3));
    clear_bits(DACDIGCON0, BIT(0) | BIT(10));
    clear_bits(AUBUFCON, BIT(4));
    active = false;
    dac_output_platform_release();
}

uint32_t dac_output_write(const int16_t *pcm, uint32_t frame_count)
{
    uint32_t accepted = 0;
    if (!active || !pcm || frame_count > UINT32_MAX / 4u) return 0;
    while (accepted < frame_count) {
        if (READ(AUBUFCON) & BIT(8)) break;
        uint32_t word = (uint16_t)pcm[0] | ((uint32_t)(uint16_t)pcm[1] << 16);
        WRITE(AUBUFDATA, word);
        pcm += 2;
        ++accepted;
    }
    return accepted;
}

bool dac_output_set_volume(uint8_t percent)
{
    if (!active || percent > 100u) return false;
    WRITE(DACVOLCON, (uint32_t)percent * 327u | (2u << 16));
    return true;
}

bool dac_output_mute(bool mute)
{
    if (!active) return false;
    if (mute) set_bits(AUANGCON1, BIT(3));
    else clear_bits(AUANGCON1, BIT(3));
    return true;
}
