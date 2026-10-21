/* Recovered from bare/lib/libhal.a(fmrx_ang.o), not a vendor source release.
 * See docs/lib-analysis/test_fm_analog_recovery.py for machine-code comparison.
 * Register names/bit meanings are deliberately not invented: public SDK
 * headers do not describe the analog FM register block at 0x410..0x42c.
 * Keep individual volatile accesses and their order, including no-op RMWs.
 */
#include "fmrx_hal.h"

#define FMREG(address) (*(volatile uint32_t *)(uintptr_t)(address))
extern void hal_mdelay(uint32_t milliseconds);

/* The original ABI takes TWO arguments. Frequency is in 10 kHz units
 * (analog_init passes 8750; the band threshold is 9350). offset_selector is
 * multiplied by 228 before PLL conversion; the original passes 1 at init.
 * Its broader mode semantics are not established. All arithmetic, including
 * intermediate multiplication overflow, follows the original RV32 code.
 * Like the SDK routine, this blocks indefinitely if PLL status bit 7 stays 0.
 */
void fmrx_set_pll(uint32_t offset_selector, uint32_t frequency_10khz)
{
    uint32_t value;
    if (frequency_10khz <= 9350u)
        FMREG(0x41c) |= 0x80u;
    else
        FMREG(0x41c) &= ~0x80u;
    FMREG(0x424) = 0x8facdu;
    value = frequency_10khz * 10u + offset_selector * 228u;
    FMREG(0x428) = value * 2581u + (value * 2864u) / 26000u;
    if (offset_selector == 0u) {
        value = (FMREG(0x428) & 0xffffffu) - 0x400000u;
        if (value > 0x800000u)
            FMREG(0x420) |= 0x10u;
        else
            FMREG(0x420) &= ~0x10u;
    }
    FMREG(0x42c) = 1u;
    FMREG(0x42c) |= 0x2cu;
    FMREG(0x420) |= 2u;
    FMREG(0x420) |= 0x101u;
    FMREG(0x420) |= 0x204u;
    FMREG(0x418) |= 0x10000u;
    FMREG(0x41c) |= 0x40u;
    FMREG(0x41c) |= 1u;
    while ((FMREG(0x42c) & 0x80u) == 0u) {}
    FMREG(0x42c) |= 0x200u;
}

void fmrx_analog_init(void)
{
    FMREG(0x410) |= 3u;
    FMREG(0x418) |= 0x800000u;
    FMREG(0x418) &= ~0x400000u;
    FMREG(0x418) &= ~0x200000u;
    FMREG(0x418) |= 0x100000u;
    FMREG(0x418) &= ~0x8000u;
    FMREG(0x418) &= ~0x4000u;
    FMREG(0x418) |= 0x10u;
    hal_mdelay(1);
    FMREG(0x41c) |= 0x800u;
    FMREG(0x41c) |= 0x400u;
    FMREG(0x41c) |= 0x200u;
    FMREG(0x41c) |= 0x100u;
    FMREG(0x41c) |= 0x80u;
    FMREG(0x41c) |= 2u;
    hal_mdelay(1);
    FMREG(0x41c) &= ~0x20u;
    FMREG(0x41c) &= ~0x3f000u;
    FMREG(0x41c) |= 0x23000u;
    FMREG(0x41c) |= 0x340000u;
    FMREG(0x41c) &= ~0x400000u;
    FMREG(0x41c) |= 0x1000000u;
    FMREG(0x41c) &= ~4u;
    FMREG(0x41c) &= ~8u;
    FMREG(0x41c) &= ~0x10u;
    FMREG(0x414) &= ~0x80000u;
    FMREG(0x414) &= ~0x100000u;
    FMREG(0x414) &= ~0x40000u;
    FMREG(0x414) &= ~0x20000u;
    fmrx_set_pll(1u, 8750u);
    FMREG(0x414) &= ~0x1fu;
    FMREG(0x414) = FMREG(0x414); /* Original +0x2ae..0x2b4. */
    FMREG(0x414) |= 0x600u;
    FMREG(0x410) |= 0x20u;
    FMREG(0x410) |= 0x40u;
    FMREG(0x410) |= 0x380u;
    FMREG(0x410) &= ~0x3c00000u;
    FMREG(0x410) |= 0x2000000u;
    FMREG(0x410) &= ~0x3c0000u;
    FMREG(0x410) |= 0x380000u;
    FMREG(0x410) &= ~0x3c00u;
    FMREG(0x410) |= 0x1c00u;
    FMREG(0x410) &= ~0x3c000u;
    FMREG(0x410) |= 0xc000u;
    FMREG(0x410) |= 0x20000000u;
    FMREG(0x410) &= ~0x18000000u;
    FMREG(0x414) |= 0x40u;
    FMREG(0x414) &= ~0x20u;
    FMREG(0x410) |= 4u;
    hal_mdelay(1);
    FMREG(0x410) |= 8u;
    FMREG(0x418) |= 0x2000u;
    FMREG(0x418) &= ~0x1000u;
    FMREG(0x418) |= 4u;
    FMREG(0x418) |= 0x800u;
    FMREG(0x418) &= ~0x400u;
    FMREG(0x418) |= 0x10u;
    hal_mdelay(10);
    FMREG(0x418) |= 1u;
    hal_mdelay(10);
    FMREG(0x418) |= 2u;
}

/* Exact original behavior: no mask before OR. Callers must restrict cap to
 * 0..31 to avoid modifying neighboring control fields. */
void fmrx_set_rf_cap(uint32_t cap)
{
    FMREG(0x414) &= ~0x1fu;
    FMREG(0x414) |= cap;
}
