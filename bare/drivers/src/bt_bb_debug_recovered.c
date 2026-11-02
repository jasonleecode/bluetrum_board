/* Recovered from libbtctrl.a:bb_debug.o. Formatting and weak bindings retained. */
#include "bt_recovered_internal.h"
static const struct { char events[4][7]; char tasks[11][4]; } labels
    __attribute__((section(".com_rodata.debug"), aligned(4))) = {
        {"msg_do","msg_ng","msg_ww","tout"},
        {" ng","mgr","ll","ble"," lm"," lc"," lb"," ld"," vs","hci","max"}
    };
static const char empty[2] __attribute__((section(".rodata"), aligned(4))) = {0};
static const char format[] __attribute__((section(".rodata.str1.4"), aligned(4))) = "%s(%d):%04x(%s,%3d), %s<-%s\n";
static void BT_SEC(".com_text.debug") bb_debug_give_within_lie(uint32_t kind, uint32_t id, uint32_t from, uint32_t to)
{
    uint32_t task = id >> 10;
    my_printf(format, kind <= 3 ? labels.events[kind] : empty, from >> 8, id,
              labels.tasks[task <= 10 ? task : 10], id & 1023,
              labels.tasks[from <= 10 ? from : 10], labels.tasks[to <= 10 ? to : 10]);
}
void BT_WEAK BT_SEC(".com_text.debug") bb_debug_keep_by_type(const uint32_t *message, uint32_t kind)
{
    if (kind) {
        uintptr_t p = *message;
        bb_debug_give_within_lie(kind, BT_H(p, 8), BT_H(p, 10), BT_H(p, 12));
    }
}
void BT_WEAK BT_SEC(".com_text.debug") bb_debug_reserve_for_unit(const uint32_t *message)
{
    uintptr_t p = *message;
    bb_debug_give_within_lie(3, BT_H(p, 4), BT_H(p, 6), 0);
}
