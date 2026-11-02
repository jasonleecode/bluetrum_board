/* Recovered from libbtctrl.a:ll_util.o. */
#include "bt_recovered_internal.h"
extern uint8_t *ll_give_about_aspect[];
extern void ble_con_take_by_join(uint32_t);
uint32_t ll_util_give_to_credit(uint16_t *value)
{
    typedef uint32_t (*query_t)(uint32_t);
    if (((query_t)(uintptr_t)0x84360)(2)) return 9;
    *value = 0;
    return 0;
}
void ll_cntl_pick_without_bid(uint32_t index, uint32_t reason)
{
    typedef void (*state_t)(uint32_t, uint32_t);
    BT_H(ll_give_about_aspect[index], 14) = 20;
    BT_B(ll_give_about_aspect[index], 71) = reason;
    ((state_t)(uintptr_t)0x84364)((uint16_t)((index << 8) | 2), 20);
    ble_con_take_by_join(BT_W(ll_give_about_aspect[index], 0));
    BT_W(ll_give_about_aspect[index], 0) = 0;
}
void ll_cntl_leave_about_cup(uint8_t *out, const uint8_t *value, const uint8_t *mask)
{ for (unsigned i = 0; i < 8; ++i) out[i] = value[i] ^ mask[i & 3]; }
void *ll_util_fetch_to_staff(uint32_t index, const void *value)
{ return memcpy(ll_give_about_aspect[index] + 16, value, 5); }
void ll_util_write_inside_god(uint32_t index, uint32_t value)
{ ll_give_about_aspect[index][72] = value; }
