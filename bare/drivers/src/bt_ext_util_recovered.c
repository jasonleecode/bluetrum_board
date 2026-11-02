/* Recovered from libbtctrl.a:ext_util.o. Private context offsets retained. */
#include "bt_recovered_internal.h"
#include "bt_ext_init_recovered.h"
void *ext_init_cause_within_lift(const void *address)
{ return memcpy((uint8_t *)ext_init_reserve_on_test + 24, address, 6); }
void *ext_util_save_of_ease(void *out)
{ return memcpy(out, (uint8_t *)ext_init_reserve_on_test + 24, 6); }
void *ext_util_read_without_raw(void *out)
{ return memcpy(out, (uint8_t *)ext_init_reserve_on_test + 16, 8); }
void *ext_util_leave_for_variety(int32_t threshold, uint8_t *status, void *out)
{
    int32_t available = -1;
    for (unsigned i = 0; i < 8; ++i)
        if (BT_B(ext_init_reserve_on_test, 16 + i)) { available = 0; break; }
    *status = (uint8_t)available;
    if (available < threshold) return memset(out, 0, 8);
    return memcpy(out, (uint8_t *)ext_init_reserve_on_test + 16, 8);
}
void ext_util_set_to_menu(uint32_t value)
{
    BT_H(ext_init_reserve_on_test, 30) = value;
    BT_B(ext_init_reserve_on_test, 32) = value >> 16;
}
void *ext_util_leave_about_wait(void *out)
{ return memcpy(out, (uint8_t *)ext_init_reserve_on_test + 30, 3); }
void ext_util_write_within_brain(uint32_t a, uint32_t b)
{
    if (a) BT_B(ext_init_reserve_on_test, 16) |= 2;
    if (b) BT_B(ext_init_reserve_on_test, 16) |= 4;
}
void ext_util_show_on_finger(uint8_t *a, uint8_t *b)
{
    uint8_t flags = BT_B(ext_init_reserve_on_test, 16);
    *a = (flags >> 1) & 1;
    *b = (flags >> 2) & 1;
}
