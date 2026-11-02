/* Recovered from bare/lib/libbtctrl.a:bb.o; not a vendor source release.
 * Preserve exported names, storage layout and ordered volatile accesses.
 * ROM calls and RF/link-layer subtrees remain explicit dependencies.
 * Sleep retains the original unbounded hardware wait. Differential tests
 * verify modeled behavior, not analog timing or a complete Bluetooth port.
 */
#include "bt_core_recovered.h"
#include <stddef.h>
extern void *memset(void *, int, size_t);

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define SECTION(s) __attribute__((section(s)))
#define SLEEP SECTION(".sleep_text.bb")
#define RAM_SLEEP SECTION(".com_text.bb.sleep")
#define NOPS2() __asm__ volatile ("nop\n\tnop" ::: "memory")

/* Only offsets and widths established by the original object are named.
 * These are the ROM allocator's metadata and backing areas, not one heap.
 */
static struct {
    uint32_t pointers[3];
    uint16_t sizes[3];
    uint8_t padding[2];
    uint8_t area0[144];
    uint8_t area1[4220];
} bb_buffers __attribute__((section(".btmem.bb.buf"), aligned(4)));
static struct {
    uint16_t flags;
    uint8_t state;
    uint8_t mode;
    uint8_t context[48];
} bb_write_in_spend __attribute__((section(".btmem.bb.sniff"), aligned(4)));
uint8_t bb_get_without_vehicle[1666]
    __attribute__((section(".btmem.bthw"), aligned(4)));
static uint32_t bb_do_for_boot SECTION(".sbss");

_Static_assert(sizeof(bb_buffers) == 4384, "ROM buffer layout");
_Static_assert(offsetof(__typeof__(bb_buffers), area0) == 20, "pool 0 offset");
_Static_assert(offsetof(__typeof__(bb_buffers), area1) == 164, "pool 1 offset");
_Static_assert(sizeof(bb_write_in_spend) == 52, "sleep state layout");

extern uint8_t nanos_env[];
extern void bb_do_on_ordinary(void), bb_make_into_mother(void);
extern void bb_do_at_review(void), bb_go_on_valuable(void);
extern void bb_tell_with_base(void), bb_take_about_press(void);
extern void bb_give_into_radio(void), bb_write_without_passenger(void);
extern void bb_pick_under_strike(void), bb_sell_about_bet(void);
extern void bb_pass_in_airport(void), bb_reserve_without_volume(void);
extern void bb_save_about_song(uint32_t);
extern uint32_t bb_pass_within_library(uint32_t *);
extern uint32_t bb_get_at_attitude(uint32_t *, uint32_t);
extern uint32_t bb_send_to_mall(uint32_t *, uint32_t);
extern void bb_send_without_line(uint32_t, uint32_t);
extern void bb_set_about_agent(void), bb_show_with_stay(void *);
extern void bb_pass_about_dependent(void *), bb_find_to_afternoon(uint32_t);
extern void bb_find_with_brush(void), bb_read_within_homework(void);

/* Names denote observed argument counts, not guessed ROM semantics. */
typedef void (*rom0_t)(void);
typedef void (*rom1_t)(uint32_t);
typedef void (*rom3_t)(uint32_t, void *, uint32_t);
typedef void *(*rom_alloc_t)(uint32_t, uint32_t);
typedef void (*rom4_t)(void *, uint32_t, void *, uint32_t);
typedef uint32_t (*rom_read_t)(void);

void bthw_get_heap_info(void **heap, uint16_t **sizes, uint32_t *blocks)
{
    *heap = bb_buffers.pointers;
    *sizes = bb_buffers.sizes;
    *blocks = 3;
}

void bb_go_about_way(void)
{
    REG32(0x610) |= 1;
    REG32(0x614) &= ~1u;
    REG32(0x60c) &= ~1u;
    REG32(0x600) = 1;
    REG32(0x610) |= 2;
    REG32(0x614) &= ~2u;
    REG32(0x60c) &= ~2u;
    REG32(0x600) = 2;
}

void SECTION(".com_text.bb.putc") bb_keep_within_error(uint32_t value)
{
    uint32_t saved = REG32(0x540) & 1u;
    REG32(0x540) &= ~1u;
    for (unsigned i = 0; i < 8; ++i)
    {
        REG32(0x604) = 1;
        if (value & 0x80u) REG32(0x600) = 2;
        else REG32(0x604) = 2;
        value = (value << 1) & 255u;
        REG32(0x600) = 1;
    }
    REG32(0x540) |= saved;
}

void SECTION(".com_text.bb.btisr") bthw_soft_isr(void)
{
    ((rom0_t)(uintptr_t)0x84664)();
}

void SECTION(".com_text.bb.btisr") bthw_isr_do(void)
{
    if (REG32(0x8010)) bb_pass_in_airport();
    if (REG32(0xf010)) bb_reserve_without_volume();
}

uint32_t __attribute__((section(".text.update"), aligned(512))) bb_make_on_husband(void)
{
    return 1;
}

void bb_init(void)
{
    bb_get_without_vehicle[0] = 0;
    REG32(0x64) |= 0x40000000u;
    REG32(0x64) = (REG32(0x64) & ~0xc00u) | 0x800u;
    ((rom1_t)(uintptr_t)0x845c8)(0x11000);
    REG32(0x350) = 0x11000;
    memset(&bb_write_in_spend, 0, 52);
    ((rom3_t)(uintptr_t)0x84320)(2, bb_buffers.area0, 129);
    ((rom3_t)(uintptr_t)0x84320)(1, bb_buffers.area1, 4208);
    void *first = ((rom_alloc_t)(uintptr_t)0x84324)(28, 2);
    void *second = ((rom_alloc_t)(uintptr_t)0x84324)(72, 2);
    ((rom4_t)(uintptr_t)0x84300)(first, 7, second, 9);
    bb_do_on_ordinary();
    bb_make_into_mother();
    bb_do_at_review();
    bb_go_on_valuable();
    REG32(0x548) &= ~2u;
    REG32(0x544) |= 2;
    REG32(0x3f8) |= 1;
    REG32(0x548) &= ~2u;
    REG32(0x544) |= 2;
    REG32(0x3f8) |= 2;
}

void bb_reset(void)
{
    uint32_t saved = REG32(0x544) & 0x180u;
    REG32(0x544) &= ~0x180u;
    REG32(0x3f8) &= ~4u;
    REG32(0x3f8) |= 4;
    ((rom1_t)(uintptr_t)0x845c8)(0x11000);
    REG32(0x350) = 0x11000;
    ((rom0_t)(uintptr_t)0x84304)();
    bb_tell_with_base();
    bb_take_about_press();
    bb_give_into_radio();
    REG32(0x544) |= saved;
}

void bb_off(void)
{
    REG32(0x64) |= 0x40000000u;
    REG32(0x64) = (REG32(0x64) & ~0xc00u) | 0x800u;
    REG32(0x544) &= ~2u;
    REG32(0x3f8) &= ~3u;
    bb_save_about_song(0);
    REG32(0x3f8) &= ~4u;
    REG32(0x3f8) |= 4;
    bb_write_without_passenger();
    bb_pick_under_strike();
    bb_sell_about_bet();
    REG32(0x64) &= ~0x40000000u;
}

void SECTION(".com_text.bb.loop") bb_run_loop(void)
{
    if (!(bb_write_in_spend.flags & 1u)) ((rom0_t)(uintptr_t)0x84350)();
}

/* The original exported entry really is a single ret instruction. */
void SECTION(".com_text.bb.cvsd") bb_find_on_influence(void) {}

void __attribute__((section(".sleep_text.bb"), noinline)) bb_pass_under_iron(void)
{
    NOPS2(); NOPS2(); NOPS2();
}

uint8_t SLEEP bb_sleep(void)
{
    uint32_t duration = 16000, result = 0, auxiliary = 0;
    if (bb_write_in_spend.state == 2) return 0;
    uint32_t saved_irq = REG32(0x540) & 1u;
    REG32(0x540) &= ~1u;
    if (!((rom_read_t)(uintptr_t)0x84308)()) goto abort_sleep;
    if (bb_write_in_spend.flags) goto abort_sleep;
    if (!bb_write_in_spend.mode) duration = 800;
    uint32_t now = ((rom_read_t)(uintptr_t)0x845b8)();
    uint32_t timer = *(uint32_t *)(void *)(nanos_env + 16);
    if (timer)
    {
        uint32_t ticks = *(uint32_t *)(uintptr_t)(timer + 8);
        uint32_t target = (now + (((ticks - (now >> 4)) & 0xffffu) << 4)) & 0x07fffff0u;
        if (((now - target) & 0x07ffffffu) > 0x30bb00u)
        {
            uint32_t remaining = (target - now) & 0x07ffffffu;
            if (remaining < duration)
            {
                if (remaining <= 4) goto abort_sleep;
                duration = remaining;
            }
        }
    }
    auxiliary = bb_pass_within_library(&duration);
    if (!bb_get_at_attitude(&duration, 4)) goto abort_sleep;
    result = bb_send_to_mall(&duration, 4);
    if (!result) goto abort_sleep;
    bb_write_in_spend.state = 2;
    duration -= 2;
    bb_send_without_line(duration * 20u - 1u, bb_write_in_spend.mode);
    bb_do_for_boot += duration;
    REG32(0x3f8) |= 0x40000u;
    bb_set_about_agent();
    while (!(REG32(0x3f8) & 0x10000u)) {}
    bb_show_with_stay(bb_write_in_spend.context);

    uint32_t saved_710 = REG32(0x710);
    uint32_t saved_3b0 = REG32(0x3b0);
    REG32(0x710) = 63;
    REG32(0x3b0) &= 0xffefffffu;
    REG32(0x374) |= 0x400;
    uint32_t saved_64 = REG32(0x64);
    uint32_t saved_3a8 = REG32(0x3a8);
    REG32(0x3a8) = (REG32(0x3a8) & 0x00ffffffu) | 0x19000000u;
    REG32(0x64) |= 12;
    NOPS2();
    if ((saved_64 & 12) == 8)
    {
        if (!(saved_64 & 0x70)) REG32(0x398) &= ~0x1040u;
        else if ((saved_64 & 0x70) == 0x20) REG32(0x39c) &= ~0x1040u;
    }
    NOPS2(); NOPS2();
    REG32(0x374) &= ~0x800u;
    NOPS2();
    REG32(0x3b4) |= 4;
    NOPS2(); NOPS2();
    REG32(0x374) |= 0x800;
    NOPS2();
    REG32(0x374) &= ~0x400u;
    NOPS2();
    if ((saved_64 & 12) == 8)
    {
        if (!(saved_64 & 0x70)) REG32(0x398) |= 0x1040;
        else if ((saved_64 & 0x70) == 0x20) REG32(0x39c) |= 0x1040;
    }
    bb_pass_under_iron(); bb_pass_under_iron(); bb_pass_under_iron();
    REG32(0x3b0) = saved_3b0;
    REG32(0x3a8) = saved_3a8;
    REG32(0x64) = saved_64;
    REG32(0x710) = saved_710;
    goto finish;
abort_sleep:
    result = 0;
    auxiliary = 0;
finish:
    REG32(0x540) |= saved_irq;
    if (bb_do_for_boot > 700) bb_do_for_boot = 0;
    else result = 0;
    return (uint8_t)(result | auxiliary);
}

uint32_t SLEEP bb_keep_within_opening(void)
{
    return bb_write_in_spend.flags;
}

void __attribute__((section(".sleep_text.bb"), aligned(64))) bb_fetch_in_event(uint32_t mask)
{
    uint32_t saved = REG32(0x540) & 1u;
    REG32(0x540) &= ~1u;
    bb_write_in_spend.flags |= mask;
    REG32(0x540) |= saved;
}

void __attribute__((section(".sleep_text.bb"), aligned(64))) bb_get_without_card(uint32_t mask)
{
    uint32_t saved = REG32(0x540) & 1u;
    REG32(0x540) &= ~1u;
    bb_write_in_spend.flags &= ~mask;
    REG32(0x540) |= saved;
}

void RAM_SLEEP bb_give_on_enthusiasm(void)
{
    bb_pass_about_dependent(bb_write_in_spend.context);
    bb_find_to_afternoon(1);
    bb_write_in_spend.state = 1;
    bb_fetch_in_event(1);
    bb_find_with_brush();
}

uint32_t RAM_SLEEP bb_is_sleep(void)
{
    if (bb_write_in_spend.flags & 32)
    {
        bb_get_without_card(32);
        return 0;
    }
    return 1;
}

void RAM_SLEEP bb_get_at_clue(void)
{
    if (bb_write_in_spend.flags & 1)
    {
        bb_read_within_homework();
        bb_get_without_card(1);
    }
}
