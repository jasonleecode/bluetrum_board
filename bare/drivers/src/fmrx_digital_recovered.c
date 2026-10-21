/* Hardware FM digital controls recovered from bare/lib/libhal.a:fmrx.o.
 * Original SHA256: f4ecad3933db2792ad6327f45d446d47d4e0aadb2a3d66f82423fcdf4a55430c
 * The ordered MMIO operations match the original, including its quirks.
 * This is not a silicon timing or audio-format validation.
 */
#include "fmrx_hal.h"

static uint32_t read32(uint32_t address)
{
    return *(volatile uint32_t *)(uintptr_t)address;
}

static void write32(uint32_t address, uint32_t value)
{
    *(volatile uint32_t *)(uintptr_t)address = value;
}

/* .text+0x00. buf_size and the FIFO counter must use matching units.
 * The SDK header does not establish whether those units are bytes or samples.
 * Keep both volatile reads: the FIFO may change between comparisons.
 */
void fmrx_dac_sync(uint32_t buf_size)
{
    uint32_t third = buf_size / 3u;
    if (third >= read32(0x114)) {
        write32(0x160, 0xfffe);
    } else if (read32(0x114) >= third * 2u) {
        write32(0x160, 1);
    } else {
        write32(0x160, 0);
    }
}

/* .text+0x2c. SDK reserves val and specifies zero. It is unused here.
 * Analog initialization tunes the vendor default of 87.5 MHz.
 * Callers retune after power-on when another frequency is desired.
 */
void fmrx_power_on(uint32_t val)
{
    (void)val;
    fmrx_analog_init();
    write32(0x440, 3);
    write32(0x440, read32(0x440) | 0x1000);
    write32(0x440, read32(0x440) | 8);
    write32(0x440, read32(0x440) | 0x80000);
    write32(0x440, read32(0x440) | 0x10000);
    write32(0x440, read32(0x440) | 0x40);
    write32(0x440, read32(0x440) | 0x200000);
    write32(0x140, read32(0x140) | 0x40);
    write32(0x440, read32(0x440) & ~0x180u);
    write32(0x440, read32(0x440) | 0x80);
}

/* .text+0xba. Preserve shutdown ordering, including DMA before RF control. */
void fmrx_power_off(void)
{
    write32(0x408, 0);
    write32(0x440, 0);
    write32(0x140, read32(0x140) & ~0x40u);
    write32(0x410, 0);
    write32(0x414, 0);
    write32(0x418, 0);
    write32(0x41c, 0);
}

/* .text+0xe0. */
void fmrx_digital_stop(void)
{
    write32(0x440, read32(0x440) & ~3u);
    write32(0x140, read32(0x140) & ~0x40u);
}

/* .text+0xf8. */
void fmrx_digital_start(void)
{
    write32(0x440, read32(0x440) | 3);
    write32(0x440, read32(0x440) | 0x1000);
    write32(0x140, read32(0x140) | 0x40);
}

/* .text+0x11e. Starts asynchronous DMA and returns immediately.
 * Raw count units and completion signalling require hardware confirmation.
 * count=0 deliberately retains the original count-1 underflow; callers must
 * validate nonzero counts and keep the buffer alive until DMA has finished.
 */
void fmrx_get_audio_data(void *buffer, uint32_t count)
{
    write32(0x400, (uint32_t)(uintptr_t)buffer);
    write32(0x404, count - 1u);
    write32(0x408, 1);
}

/* .text+0x130. SDK ABI uses uint8_t enable.
 * The zero case writes 3 to 0x444, NOT a clear of bit 2 at 0x440.
 * Preserve that command without assuming undocumented register semantics.
 */
void fmrx_dma_to_aubuf(uint8_t enable)
{
    if (enable)
        write32(0x440, read32(0x440) | 4);
    else
        write32(0x444, 3);
}
