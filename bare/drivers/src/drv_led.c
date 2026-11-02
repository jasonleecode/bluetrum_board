#include "drv_led.h"
#include <stdint.h>

#define REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define RED (1u << 1)

void drv_led_init(void)
{
    REG32(0x3b0) |= 1u << 17; /* CLKGAT0: port clock */
    REG32(0x680) = RED;       /* GPIOESET: preload off before enabling output */
    REG32(0x694) &= ~RED;     /* GPIOEFEN: ordinary GPIO */
    REG32(0x69c) &= ~RED;     /* GPIOEPU */
    REG32(0x6a0) &= ~RED;     /* GPIOEPD */
    REG32(0x690) |= RED;      /* GPIOEDE: digital enable */
    REG32(0x68c) &= ~RED;     /* GPIOEDIR: 0 = output */
}

void drv_led_set(bool on)
{
    /* Atomic set/clear preserves the other seven port pins. */
    REG32(on ? 0x684 : 0x680) = RED;
}
