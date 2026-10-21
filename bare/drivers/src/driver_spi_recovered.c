/* Hardware SPI1 reconstructed from bare/lib/libhal.a:driver_spi.o.
 * SHA256: 84e5ce25a4368d749c0efd9f08f19ba5ca9f44698d477ed59dbedefffd2564bd
 * See docs/lib-analysis/SPI_I2C_RECOVERY.md for offsets and limitations.
 * This deliberately preserves observed SDK behavior, including quirks.
 * No vendor functions from driver_spi.o are called by this implementation.
 */
#include "blue_spi.h"
#include <stddef.h>

enum {
    FUNCMCON1 = 0x020,
    SPI1CON = 0x980, SPI1BUF = 0x984, SPI1BAUD = 0x988,
    SPI1CPND = 0x98c, SPI1DMACNT = 0x990, SPI1DMAADR = 0x994,
    ENABLE = 1, RECEIVE = 0x10, DMA_ENABLE = 0x80, PENDING = 0x10000,
};

static uint32_t read32(uint32_t address)
{
    return *(volatile uint32_t *)(uintptr_t)address;
}
static void write32(uint32_t address, uint32_t value)
{
    *(volatile uint32_t *)(uintptr_t)address = value;
}

typedef void (*isr_t)(void);
extern isr_t register_isr(int vector, isr_t handler);
extern uint32_t get_sysclk_nhz(void);
extern uint32_t hal_get_ticks(void);
extern void os_interrupt_enter(void);
extern void os_interrupt_leave(void);

static uint32_t config; /* _config: 4 bytes, only low byte subsequently used. */
static blue_spi_signal_event_t spi_handle;
static blue_spi_version_t driver_version = {0x0100, 0x0100};

static blue_spi_version_t *blue_get_version(void) /* .text+0x0 */
{
    return &driver_version;
}

static int32_t blue_spi_deinit(void) /* .text+0x0a */
{
    write32(SPI1CON, 0);
    spi_handle = NULL;
    return 0;
}

static int32_t blue_spi_power_control(uint32_t state) /* .text+0x1c */
{
    (void)state; /* Original always returns success without touching hardware. */
    return 0;
}

/* .text+0x20. GPIO offsets: DIR=12, DE=16, FEN=20, PU=28, PD=32. */
static void spi_io_mapping(uint32_t simplex, uint32_t master, uint32_t port,
                           uint32_t input, uint32_t clock, uint32_t output,
                           uint32_t mapping)
{
    if (!simplex) {
        uint32_t bit = 1u << input;
        write32(port + 20, read32(port + 20) | bit);
        write32(port + 16, read32(port + 16) | bit);
        write32(port + 12, read32(port + 12) | bit);
        write32(port + 28, read32(port + 28) | bit);
    }
    uint32_t clk = 1u << clock, out = 1u << output;
    write32(port + 20, read32(port + 20) | clk | out);
    write32(port + 16, read32(port + 16) | clk | out);
    if (master) {
        write32(port + 12, read32(port + 12) & ~clk);
    } else {
        write32(port + 12, read32(port + 12) | clk);
        write32(port + 32, read32(port + 32) | clk);
    }
    write32(port + 12, read32(port + 12) & ~out);
    write32(FUNCMCON1, read32(FUNCMCON1) | (mapping << 12));
}

static void spi_irq_handle(void) /* .com_text.spi+0x4c */
{
    os_interrupt_enter();
    if (read32(SPI1CON) & PENDING) {
        write32(SPI1CPND, PENDING);
        write32(SPI1CON, read32(SPI1CON) & ~(DMA_ENABLE | RECEIVE));
        if (spi_handle) spi_handle(BLUE_SPI_EVENT_COMPLETE);
    }
    os_interrupt_leave();
}

/* Preserve RV32 DIVU by zero semantics without C division UB. */
static uint32_t divider(uint32_t hz)
{
    uint32_t clock = get_sysclk_nhz();
    return (hz ? clock / hz : UINT32_MAX) - 1u;
}

static int32_t blue_spi_control(uint32_t control, uint32_t arg) /* .text+0x90 */
{
    uint32_t command = control & 255u;
    if (!command) return 0;
    if (command < 32) {
        write32(SPI1CON, 0);
        switch (command) {
        case BLUE_SPI_MODE_MASTER:
            write32(SPI1BAUD, divider(arg));
            config |= 4u;
            if (config & 2u) config = (config & ~2u) | 1u;
            break;
        case BLUE_SPI_MODE_SLAVE:
            write32(SPI1CON, 2);
            config &= ~4u;
            if (config & 2u) config = (config & ~2u) | 1u;
            break;
        case BLUE_SPI_MODE_MASTER_SIMPLEX:
            write32(SPI1BAUD, divider(arg));
            write32(SPI1CON, 4);
            config |= 4u;
            if (!(config & 2u)) config |= 7u;
            break;
        default:
            return -1;
        }
        register_isr(20, spi_irq_handle);
    }

    /* Original compares the full control value here, not the low byte. */
    if (control == BLUE_SPI_SET_MAPPING) {
        /* Mapping fields have command semantics: 0xf clears the selection.
         * Keep both writes, matching gpio_afinit and the vendor object. */
        write32(FUNCMCON1, read32(FUNCMCON1) | 0xf000u);
        config &= ~1u;
        switch (arg) {
        case 1: spi_io_mapping((config >> 1) & 1, (config >> 2) & 1,
                               0x600, 2, 3, 4, 1); break;
        case 2: spi_io_mapping((config >> 1) & 1, (config >> 2) & 1,
                               0x600, 5, 6, 7, 2); break;
        case 3: spi_io_mapping((config >> 1) & 1, (config >> 2) & 1,
                               0x640, 0, 1, 2, 3); break;
        case 4: spi_io_mapping((config >> 1) & 1, (config >> 2) & 1,
                               0x680, 5, 6, 7, 4); break;
        case 5: spi_io_mapping((config >> 1) & 1, (config >> 2) & 1,
                               0x6c0, 0, 1, 2, 5); break;
        default: break; /* Original returns success even for invalid mapping. */
        }
    } else if (control == BLUE_SPI_ENABLE) {
        if (config & 1u) return -1;
        write32(SPI1CON, read32(SPI1CON) | ENABLE);
    }
    return 0;
}

static int32_t blue_spi_init(blue_spi_signal_event_t callback) /* .text+0x22a */
{
    config = 1;
    spi_handle = callback;
    write32(SPI1CON, 0);
    return 0;
}

static int32_t blue_spi_send(const void *data, uint32_t num) /* .com_text.spi+0 */
{
    if (!(read32(SPI1CON) & ENABLE)) return -1;
    write32(SPI1DMAADR, (uint32_t)(uintptr_t)data);
    write32(SPI1DMACNT, num);
    write32(SPI1CON, read32(SPI1CON) | DMA_ENABLE);
    return 0;
}

static int32_t blue_spi_recv(void *data, uint32_t num) /* .com_text.spi+0x26 */
{
    if (!(read32(SPI1CON) & ENABLE)) return -1;
    /* Ordering differs from send: preserve the original sequence. */
    write32(SPI1CON, read32(SPI1CON) | DMA_ENABLE | RECEIVE);
    write32(SPI1DMAADR, (uint32_t)(uintptr_t)data);
    write32(SPI1DMACNT, num);
    return 0;
}

static int32_t blue_spi_recv_byte(int32_t timeout) /* .com_text.spi+0x9a */
{
    uint32_t deadline = hal_get_ticks() + (uint32_t)timeout;
    write32(SPI1CON, read32(SPI1CON) | RECEIVE);
    write32(SPI1BUF, 0xff);
    for (;;) {
        if (read32(SPI1CON) & PENDING) return (int32_t)read32(SPI1BUF);
        if (timeout > 0 && hal_get_ticks() >= deadline) return -1;
    }
}

static int32_t blue_spi_send_byte(uint8_t value, int32_t timeout) /* +0xf4 */
{
    uint32_t deadline = hal_get_ticks() + (uint32_t)timeout;
    write32(SPI1CON, read32(SPI1CON) & ~RECEIVE);
    write32(SPI1BUF, value);
    for (;;) {
        if (read32(SPI1CON) & PENDING) return 0;
        if (timeout > 0 && hal_get_ticks() >= deadline) return -1;
    }
}

/* Original .rodata+0x14: nine function-pointer relocations in this order. */
const struct blue_drv_spi blue_spi1 = {
    blue_get_version, blue_spi_init, blue_spi_deinit, blue_spi_power_control,
    blue_spi_send, blue_spi_recv, blue_spi_send_byte, blue_spi_recv_byte,
    blue_spi_control
};
