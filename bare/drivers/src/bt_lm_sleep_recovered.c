/* Recovered from libbtctrl.a:lm_sleep.o. ROM scheduler remains external. */
#include "bt_recovered_internal.h"
void ext_init_get_without_name(void)
{
    BT_REG(0x803c) = 0x0140280a;
    BT_REG(0x8030) &= UINT32_C(0x7fffffff);
}
uint32_t BT_SEC(".sleep_text.sleep") bb_send_to_mall(uint32_t *duration, uint32_t margin)
{
    typedef uint32_t (*sleep_t)(uint32_t *, uint32_t);
    return ((sleep_t)(uintptr_t)0x844f4)(duration, margin);
}
void BT_SEC(".sleep_text.sleep") bb_send_without_line(uint32_t duration, uint32_t option)
{
    BT_REG(0x8034) = duration;
    BT_REG(0x800c) = 4;
    BT_REG(0x8018) = 7;
    if (!option) BT_REG(0x8030) |= UINT32_C(0x80000000);
}
void BT_SEC(".com_text.bb.sleep") bb_find_with_brush(void)
{
    BT_REG(0x8030) &= UINT32_C(0x7fffffff);
    uint32_t value = BT_REG(0x8038);
    value = value * 31u + (value >> 2);
    BT_REG(0x8044) = (value / 625) << 1;
    BT_REG(0x8040) = value % 625;
    BT_REG(0x8044) = (BT_REG(0x8044) & UINT32_C(0x7fffffff)) | UINT32_C(0x80000000);
    BT_REG(0x8030) |= 8;
    BT_REG(0x800c) = UINT32_C(0xffff0003);
}
void BT_SEC(".com_text.bb.sleep") bb_read_within_homework(void) {}
