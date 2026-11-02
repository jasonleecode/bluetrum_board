/* Recovered from libbtctrl.a:bb_buf.o. Preserve the contiguous 80-byte ABI. */
#include "bt_recovered_internal.h"
static struct { uint8_t *control, *queues; uint8_t nodes[72]; } buffers
    __attribute__((section(".btmem.bb.util"), aligned(4), used)) = {0};
/* Original exported objects are aliases into one contiguous allocation. */
__asm__(".global ble_con_keep_inside_cell\n.type ble_con_keep_inside_cell,@object\n"
        ".pushsection .btmem.bb.util,\"aw\",@progbits\n.balign 4\nble_con_keep_inside_cell:\n.popsection\n.size ble_con_keep_inside_cell,4\n"
        ".global bb_buf_reserve_about_union\n.type bb_buf_reserve_about_union,@object\n"
        ".set bb_buf_reserve_about_union,buffers+4\n.size bb_buf_reserve_about_union,4\n"
        ".global bb_buf_fetch_about_money\n.type bb_buf_fetch_about_money,@object\n"
        ".set bb_buf_fetch_about_money,buffers+8\n.size bb_buf_fetch_about_money,72\n");
void ll_sell_under_data(uint32_t index)
{
    uintptr_t entry = BT_W(buffers.control, 20) + index * 46u;
    BT_H(entry, 0) &= 0x7fff;
}
void BT_SEC(".text.buf.init") bt21_find_on_you(void)
{
    typedef void (*get_t)(uint8_t **, uint8_t **);
    typedef void (*queue_t)(void *, uintptr_t, uint32_t, uint32_t);
    typedef void (*init_t)(void *);
    typedef void (*append_t)(void *, void *);
    ((get_t)(uintptr_t)0x845a4)(&buffers.queues, &buffers.control);
    ((queue_t)(uintptr_t)0x843e8)(buffers.queues + 4, 0x11648, 692, 0);
    ((queue_t)(uintptr_t)0x843e8)(buffers.queues, 0x11648, 692, 0);
    ((queue_t)(uintptr_t)0x843e8)(buffers.queues + 8, 0x11648, 28, 0);
    BT_B(buffers.control, 24) = 0;
    ((init_t)(uintptr_t)0x843c0)(buffers.control);
    BT_W(buffers.control, 12) = 0x11354;
    BT_W(buffers.control, 16) = 0x1163e;
    BT_B(buffers.control, 25) = 8;
    BT_B(buffers.control, 26) = 9;
    BT_W(buffers.control, 20) = 0x114ce;
    BT_W(buffers.control, 8) = (uintptr_t)buffers.nodes;
    BT_B(buffers.control, 27) = 5;
    for (unsigned i = 0; i < 9; ++i) {
        uint8_t *node = (uint8_t *)(uintptr_t)BT_W(buffers.control, 8) + i * 8;
        BT_H(node, 4) = i;
        if (i <= 4) ((append_t)(uintptr_t)0x843c8)(buffers.control, node);
    }
    uintptr_t entries = BT_W(buffers.control, 20);
    for (unsigned i = 0; i < 8; ++i)
        BT_H(entries + i * 46, 0) = (entries + ((i + 1) & 7) * 46 - 0x1000) & 0x3fff;
}
