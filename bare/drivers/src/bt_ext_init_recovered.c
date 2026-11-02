/* Recovered from bare/lib/libbtctrl.a:ext_init.o, not vendor source.
 * Fixed RAM addresses and ROM calls remain part of the original ABI.
 * See docs/lib-analysis/BLUETOOTH_RECOVERY.md for evidence and limitations.
 */
#include "bt_ext_init_recovered.h"
#include <stddef.h>

#define REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define REG16(address) (*(volatile uint16_t *)(uintptr_t)(address))

extern void *memset(void *, int, size_t);
extern void bt_get_local_bd_addr(uint8_t *address);
extern void ext_init_cause_within_lift(uint8_t *address);
extern void bt21_find_on_you(void);
extern void ext_init_get_without_name(void);
extern void bb_take_about_press(void);
extern void my_printf(const char *format, ...);

uint32_t ext_init_reserve_on_test[9]
    __attribute__((section(".btmem.bb"), aligned(4))) = {0};
uint8_t ext_init_do_of_weather
    __attribute__((section(".btmem.bb.rf"))) = 0;

static const uint32_t ext_init_table[6]
    __attribute__((section(".rodata.bb.tbl"), aligned(4))) = {
        UINT32_C(0x00060010), 0, 0, 0,
        UINT32_C(0xfa4d2ebf), UINT32_C(0x877b3dd8)
    };

static void set_local_address(void)
{
    uint8_t address[6];
    bt_get_local_bd_addr(address);
    for (unsigned i = 0; i < 3; ++i)
    {
        uint8_t byte = address[i];
        address[i] = address[5 - i];
        address[5 - i] = byte;
    }
    ext_init_cause_within_lift(address);
}

static void initialize_slots(void)
{
    /* Preserve all sixteen halfword stores, including their access width. */
    for (uintptr_t address = 0x11000; address < 0x11020; address += 2)
        REG16(address) = 0x6000;
}

void bb_write_without_passenger(void)
{
    REG32(0x8000) &= ~UINT32_C(0x100);
    REG32(0x8000) |= UINT32_C(0x80000000);
    while (REG32(0x8000) & UINT32_C(0x80000000)) {}
    REG32(0x8000) = (REG32(0x8000) & UINT32_C(0xbfffffff)) |
                    UINT32_C(0x40000000);
    while (REG32(0x8000) & UINT32_C(0x40000000)) {}
    initialize_slots();
    REG32(0x800c) = 0;
    REG32(0x8018) = UINT32_C(0xffffffff);
}

void bt21_set_on_tennis(void)
{
    typedef void (*rom_three_t)(uintptr_t, uintptr_t, uint32_t);
    typedef void (*rom_two_t)(uintptr_t, uintptr_t);
    typedef void (*rom_table_t)(const uint32_t *);

    ext_init_do_of_weather = 0;
    ((rom_three_t)(uintptr_t)0x845ac)(0x11644, 0x11658, 2);
    ((rom_three_t)(uintptr_t)0x845a8)((uintptr_t)ext_init_reserve_on_test, 0, 0);
    ((rom_two_t)(uintptr_t)0x845c0)(0, 0);
    ((rom_three_t)(uintptr_t)0x845c4)(0, 0, 0);
    ((rom_table_t)(uintptr_t)0x845cc)(ext_init_table);
    memset(ext_init_reserve_on_test, 0, sizeof(ext_init_reserve_on_test));
    ext_init_reserve_on_test[0] = 0x11190;
    ext_init_reserve_on_test[1] = 0x111e8;
    ext_init_reserve_on_test[2] = 0x11240;
    set_local_address();
    initialize_slots();
    bt21_find_on_you();
    REG32(0x80c0) = 0;
    ext_init_get_without_name();
    ext_init_reserve_on_test[4] = ext_init_table[4];
    ext_init_reserve_on_test[5] = ext_init_table[5];
    REG32(0x8000) &= UINT32_C(0xfebf8fff);
    REG32(0x8000) |= UINT32_C(0x00200000);
    REG32(0x8000) |= UINT32_C(0x17);
    REG32(0x800c) = UINT32_C(0xffff0003);
    REG32(0x81b0) = UINT32_C(0x801e);
    REG32(0x8000) |= UINT32_C(0x100);
}

void ext_init_reserve_to_word(uint32_t clock)
{
    typedef uint32_t (*read_clock_t)(void);
    set_local_address();
    REG32(0x80c0) = 0;
    REG32(0x8000) &= UINT32_C(0xfebf8fff);
    REG32(0x8000) |= UINT32_C(0x00200000);
    REG32(0x8000) |= UINT32_C(0x1f);
    REG32(0x800c) = UINT32_C(0xffff0003);
    REG32(0x81b0) = UINT32_C(0x801e);
    REG32(0x8000) |= UINT32_C(0x100);
    REG32(0x8020) = clock | UINT32_C(0x80000000);
    while (REG32(0x8020) & UINT32_C(0x80000000)) {}
    my_printf("hwbt_clk: %08lx\n",
              (unsigned long)((read_clock_t)(uintptr_t)0x845b8)());
}

void ext_init_pick_of_edge(void)
{
    typedef uint32_t (*read_clock_t)(void);
    uint32_t saved = REG32(0x544) & UINT32_C(0x180);
    REG32(0x544) &= ~UINT32_C(0x180);
    uint32_t clock = ((read_clock_t)(uintptr_t)0x845b8)();
    bb_write_without_passenger();
    bb_take_about_press();
    ext_init_reserve_to_word(clock);
    REG32(0x544) |= saved;
}

void ext_init_pass_within_equivalent(uint32_t disable)
{
    my_printf("set_tx_disable: %d\n", (int32_t)disable);
    uint8_t *flags = (uint8_t *)(uintptr_t)ext_init_reserve_on_test[0] + 12;
    if (disable)
        *flags |= 0x10;
    else
        *flags &= (uint8_t)~0x10;
}
