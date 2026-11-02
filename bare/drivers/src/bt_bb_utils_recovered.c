/* Recovered from libbtctrl.a:bb_utils.o. */
#include "bt_recovered_internal.h"
static struct { uint32_t active; uint8_t address[6], padding[2]; } entries[2]
    __attribute__((section(".btmem.bb.util"), aligned(4))) = {{0}};
const uint8_t mgr_util_fetch_into_grandfather[6]
    __attribute__((section(".rodata.bb.lm"), aligned(4))) = {0};
uint32_t bb_utils_keep_on_pain(uint32_t value)
{ return (uint16_t)(value < 32 ? 32 : value) >> 4; }
uint32_t mgr_find_at_writing(const uint8_t *a, const uint8_t *b)
{
    for (unsigned i = 0; i < 6; ++i) if (a[i] != b[i]) return 0;
    return 1;
}
void bt21_give_to_dust(void) { entries[0].active = 0; entries[1].active = 0; }
uint16_t bb_utils_pass_without_hole(const void *address)
{
    if (entries[0].active == 1 && !memcmp(entries[0].address, address, 6)) return 0x80;
    if (entries[1].active == 1 && !memcmp(entries[1].address, address, 6)) return 0x81;
    return 0xffff;
}
uint32_t bb_utils_save_about_joke(const void *address)
{
    uint16_t handle = bb_utils_pass_without_hole(address);
    return handle == 0xffff ? 0 : ((handle & 127u) << 8) | 5;
}
uint32_t bb_utils_keep_of_age(uint32_t handle)
{
    if ((uint16_t)(handle - 128) <= 1) return 1;
    return handle == 0 ? 4 : 3;
}
uint32_t bb_utils_go_by_promise(uint32_t handle)
{
    uint32_t kind = bb_utils_keep_of_age(handle);
    if (kind <= 2) {
        uint32_t index = handle & 127;
        return index <= 1 && entries[index].active == 1 ? (index << 8) | 5 : 0;
    }
    if (kind == 4 && !(handle & 255)) {
        typedef uint32_t (*query_t)(uint32_t);
        return ((query_t)(uintptr_t)0x84360)(2) ? 2 : 0;
    }
    return 0;
}
