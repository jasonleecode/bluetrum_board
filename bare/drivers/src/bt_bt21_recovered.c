/* Recovered from libbtctrl.a:bt21.o. */
#include "bt_recovered_internal.h"
extern void bt21_give_to_dust(void), bt21_find_on_you(void), bt21_save_under_dig(void);
extern void bt21_set_on_tennis(void), bt21_keep_within_grocery(void), bt21_go_about_pool(void);
extern void bb_write_without_passenger(void), bb_get_at_clue(void), bt21_save_in_design(void);
extern void bb_give_on_enthusiasm(void), bb_find_to_afternoon(uint32_t);
void bb_do_at_review(void)
{
    bt21_give_to_dust(); bt21_find_on_you(); bt21_save_under_dig();
    bt21_set_on_tennis(); bt21_keep_within_grocery();
}
void bb_tell_with_base(void)
{
    uint32_t saved = BT_REG(0x544);
    BT_REG(0x544) &= ~UINT32_C(0x180);
    bt21_go_about_pool(); bb_write_without_passenger();
    bt21_set_on_tennis(); bt21_keep_within_grocery();
    BT_REG(0x544) |= saved & 0x180;
}
void BT_SEC(".com_text.bb.btisr") bb_pass_in_airport(void)
{
    uint32_t pending = BT_REG(0x8010);
    if (pending & 1) { bb_get_at_clue(); BT_REG(0x8018) = 1; bt21_save_in_design(); }
    if (pending & 2) BT_REG(0x8018) = 2;
    if (pending & 4) { BT_REG(0x8018) = 4; bb_give_on_enthusiasm(); }
    if (pending & 0x400) { BT_REG(0x8018) = 0x400; bb_find_to_afternoon(1); }
}
