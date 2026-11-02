#include "heartbeat_board.h"

#define REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define BIT(n) (1u << (n))

/* All code executes in SRAM. Select the board's 26 MHz crystal without a PLL;
 * polling at 26 MHz can service the unbuffered UART at 115200 baud.
 */
void board_init(void)
{
    REG32(0x68) = 0xaa0;             /* disable WDT during setup */
    REG32(0x64) &= ~(BIT(2) | BIT(3)); /* switch through RC2M */
    REG32(0x3a8) &= ~(31u << 8);     /* SPLL divider = 1 */
    REG32(0x64) = (REG32(0x64) & ~(7u << 4)) | BIT(6); /* XOSC26M */
    REG32(0x64) |= BIT(3);
    REG32(0x3b0) |= BIT(8) | BIT(10) | BIT(17);
    REG32(0x50) = 0;                /* timer0: system clock, no interrupts */
    REG32(0x58) = 0;
    REG32(0x5c) = 0xffffffffu;
    REG32(0x54) = BIT(9);
    REG32(0x50) = 1;

    REG32(0x40) = 0;                /* UART0: PA7 single wire, 115200 8N1 */
    REG32(0x74) |= BIT(14);         /* enable oscillator UART clock */
    REG32(0x1c) = (REG32(0x1c) & ~0xff00u) | 0x7100u;
    REG32(0x60c) |= BIT(7);         /* input direction for single-wire UART */
    REG32(0x610) |= BIT(7);
    REG32(0x614) |= BIT(7);
    REG32(0x61c) |= BIT(7);
    REG32(0x620) &= ~BIT(7);
    REG32(0x48) = (112u << 16) | 112u; /* 26 MHz / 2 / (112+1) */
    REG32(0x44) = BIT(8) | BIT(9);
    REG32(0x40) = BIT(0) | BIT(5) | BIT(6) | BIT(7);
    REG32(0x68) = (0xau << 24) | (4u << 20); /* watchdog ~2 seconds */
    REG32(0x68) = 0x11a;
}

void board_watchdog_feed(void)
{
    REG32(0x68) = 0xa;
}

/* Preserve the downloader's software-reset key while the application runs.
 * The boot header, retained by normal DCF packaging, handles ROM re-entry.
 */
void board_reset(void)
{
    REG32(0x68) = 0xau << 24;
    REG32(0x68) = 0x11a;
    for (;;) { __asm__ volatile ("nop"); }
}

int board_uart_getc(void)
{
    static const uint8_t key[] = {0xf5, 0xa0, 0xa5, 0x96, 0x87, 0x5a};
    static unsigned matched;
    if (REG32(0x40) & BIT(9)) {
        uint8_t value = REG32(0x4c);
        REG32(0x44) = BIT(9);
        matched = value == key[matched] ? matched + 1 : (value == key[0]);
        if (matched == sizeof(key))
            board_reset();
        return value;
    }
    return -1;
}

uint32_t board_millis(void)
{
    static uint32_t last, remainder, milliseconds;
    uint32_t now = REG32(0x58);
    uint32_t delta = now - last;
    last = now;
    /* Update frequently so the timer's 165-second wrap cannot be missed. */
    milliseconds += delta / 26000u;
    remainder += delta % 26000u;
    milliseconds += remainder / 26000u;
    remainder %= 26000u;
    return milliseconds;
}

void board_delay_ms(uint32_t ms)
{
    uint32_t start = board_millis();
    while ((uint32_t)(board_millis() - start) < ms)
        (void)board_uart_getc();
}

void board_puts(const char *text)
{
    /* Do not feed our own transmission back into the command parser. The PC
     * must finish sending before this half-duplex reply starts.
     */
    /* RXPND can precede the end of the host's last stop bit. Give the CH340
     * time to release the shared wire, including an optional LF after CR.
     */
    uint32_t guard = REG32(0x58);
    while ((uint32_t)(REG32(0x58) - guard) < 26000u) { }
    REG32(0x40) &= ~BIT(7);
    while (*text) {
        REG32(0x44) = BIT(8);
        REG32(0x4c) = (uint8_t)*text++;
        uint32_t start = REG32(0x58);
        while (!(REG32(0x40) & BIT(8))) {
            if ((uint32_t)(REG32(0x58) - start) > 260000u)
                goto done; /* A UART fault must not prevent the heartbeat. */
        }
    }
done:
    REG32(0x44) = BIT(9);
    REG32(0x40) |= BIT(7);
}
