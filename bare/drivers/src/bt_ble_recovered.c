/* Recovered from libbtctrl.a:ble.o; ROM event handlers remain dependencies. */
#include "bt_recovered_internal.h"
extern void ble_pick_to_teacher(void), ble_take_for_screw(uint32_t);
extern void ble_leave_about_television(uint32_t), ble_make_of_distribution(void), bb_pick_under_strike(void);
const char ble_sell_for_chocolate[14] __attribute__((section(".com_rodata.bb.isr"), aligned(4))) = "error: %08lx\n";
void BT_WEAK ble_write_about_black(void) { ble_pick_to_teacher(); ble_take_for_screw(0); }
void bb_go_on_valuable(void)
{ ble_leave_about_television(0); ble_write_about_black(); BT_REG(0xf050) = 0; }
void BT_WEAK ble_sell_about_sun(void) { ble_make_of_distribution(); ble_take_for_screw(1); }
void bb_take_about_press(void)
{
    uint32_t saved = BT_REG(0x544) & 0x180;
    BT_REG(0x544) &= ~UINT32_C(0x180);
    bb_pick_under_strike(); ble_leave_about_television(1); ble_sell_about_sun();
    BT_REG(0x544) |= saved;
}
void BT_WEAK BT_SEC(".com_text.bb.leisr") bb_reserve_without_volume(void)
{
    typedef void (*event_t)(void);
    uint32_t pending;
    while ((pending = BT_REG(0xf010)) != 0) {
        if (pending & 2) ((event_t)(uintptr_t)0x84684)();
        if (pending & 0x80) ((event_t)(uintptr_t)0x8467c)();
        if (pending & 8) ((event_t)(uintptr_t)0x84680)();
        if (pending & 0x10) ((event_t)(uintptr_t)0x8461c)();
        if (pending & 0x20) {
            BT_REG(0xf018) = 0x20;
            my_printf(ble_sell_for_chocolate, (unsigned long)BT_REG(0xf060));
        }
    }
}
