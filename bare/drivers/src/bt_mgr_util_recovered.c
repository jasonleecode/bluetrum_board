/* Recovered from libbtctrl.a:mgr_util.o. Manager context offsets retained. */
#include "bt_recovered_internal.h"
extern uint8_t mgr_cause_within_nobody[], mgr_take_with_try[];
extern const uint8_t mgr_util_fetch_into_grandfather[];
extern uint32_t mgr_find_at_writing(const void *, const void *);
extern void ble_con_make_on_burn(const void *, uint32_t), ble_get_local_bd_addr(uint8_t *);
static const struct __attribute__((packed)) { uint16_t count; uint8_t pad[2]; uint8_t values[3]; } util_constants
    __attribute__((section(".rodata.bb.lm"), aligned(4), used)) = {12, {0,0}, {2,4,6}};
__asm__(".global mgr_read_for_boy\n.type mgr_read_for_boy,@object\n.pushsection .rodata.bb.lm,\"a\",@progbits\n.balign 4\nmgr_read_for_boy:\n.popsection\n.size mgr_read_for_boy,2\n"
        ".global mgr_take_inside_band\n.type mgr_take_inside_band,@object\n.set mgr_take_inside_band,util_constants+4\n.size mgr_take_inside_band,3\n");
uint32_t mgr_write_in_difference(const void *address, uint32_t bank, uint16_t *index)
{
    uint8_t copy[6];
    uintptr_t base = bank ? 0x1166a : 0x11658;
    for (unsigned i = 0; i < 3; ++i) {
        memcpy(copy, (void *)(base + i * 6), 6);
        uint32_t equal = mgr_find_at_writing(copy, address);
        if (equal) { *index = i; return equal; }
    }
    *index = 3;
    return 0;
}
uint32_t mgr_take_on_diamond(const void *address)
{ return mgr_find_at_writing(address, mgr_util_fetch_into_grandfather) ? 18 : 0; }
void mgr_make_within_entrance(uint32_t kind)
{ ble_con_make_on_burn(mgr_cause_within_nobody + (kind == 1 ? 64 : 70), kind == 1 ? 1 : 0); }
void mgr_util_pick_with_refrigerator(void) { ble_get_local_bd_addr(mgr_cause_within_nobody + 70); }
void mgr_write_at_other(void)
{
    for (unsigned offset = 0; offset < 70; offset += 7) {
        memset(mgr_cause_within_nobody + 78 + offset, 0, 6);
        mgr_cause_within_nobody[77 + offset] = 0;
    }
}
uint32_t ll_save_under_decision(void) { return mgr_cause_within_nobody[44]; }
void mgr_proc_fetch_at_novel(uint32_t value) { mgr_cause_within_nobody[44] = value; }
void *ll_read_of_yellow(void *out) { return memcpy(out, mgr_cause_within_nobody + 59, 5); }
void *ll_take_into_culture(void *out) { return memcpy(out, mgr_take_with_try, 8); }
uint32_t BT_SEC(".com_text.bb.util") ble_con_do_without_notice(const uint8_t *bits, uint32_t length)
{
    if ((bits[0] & 0xf8) && length == 1) return 0;
    uint8_t index = 0, count = 0;
    while (index != length) {
        if (index == 4 && (bits[0] & 0xe0)) return 38;
        uint32_t byte = *bits++;
        for (unsigned bit = 0; bit < 8; ++bit) count += (byte >> bit) & 1;
        ++index;
    }
    return count;
}
uint32_t BT_SEC(".com_text.bb.leisr") ll_reserve_without_bathroom(uint32_t bit)
{ return (mgr_cause_within_nobody[32 + (bit >> 3)] >> (bit & 7)) & 1; }
