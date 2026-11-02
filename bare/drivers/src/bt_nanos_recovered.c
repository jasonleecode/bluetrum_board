/* Recovered from bare/lib/libbtctrl.a:nanos.o, not vendor source.
 * Preserve the original names, callback ABI and ordered volatile accesses.
 * ROM scheduler services remain dependencies; see BLUETOOTH_RECOVERY.md.
 */
#include "bt_nanos_recovered.h"

#define REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define NANOS_RAM __attribute__((section(".com_text.bb.nanos")))

extern void bthw_soft_kick(void);
extern void nanos_event_set_trigger(void);

uint32_t nanos_keep_inside_reach(void)
{
    return 0;
}

uint32_t lm_proc_do_on_kiss(void)
{
    return 2;
}

void bb_do_on_ordinary(void)
{
    typedef void (*register_timer_t)(uint32_t (*)(void), void (*)(const void *));
    typedef void (*register_event_t)(void (*)(uint32_t));
    ((register_timer_t)(uintptr_t)0x84390)(nanos_save_to_draft,
                                        nanos_write_inside_process);
    ((register_event_t)(uintptr_t)0x8435c)(bb_find_to_afternoon);
}

uint32_t NANOS_RAM nanos_save_to_draft(void)
{
    typedef uint32_t (*read_time_t)(void);
    return (((read_time_t)(uintptr_t)0x845b8)() >> 4) & UINT32_C(0xffff);
}

void NANOS_RAM bb_find_to_afternoon(uint32_t event)
{
    typedef void (*dispatch_t)(uint32_t);
    if (event == 6)
        bthw_soft_kick();
    else
        ((dispatch_t)(uintptr_t)0x84344)(event);
    nanos_event_set_trigger();
}

void NANOS_RAM nanos_write_inside_process(const void *timer)
{
    if (timer)
    {
        /* Original ABI reads a halfword at offset 8. No complete public
         * timer structure definition is available; do not invent one.
         */
        REG32(0x80e0) = *(const uint16_t *)((const uint8_t *)timer + 8);
        if (!(REG32(0x800c) & UINT32_C(0x400)))
        {
            REG32(0x8018) = UINT32_C(0x400);
            REG32(0x800c) |= UINT32_C(0x400);
        }
    }
    else
        REG32(0x800c) &= ~UINT32_C(0x400);
}
