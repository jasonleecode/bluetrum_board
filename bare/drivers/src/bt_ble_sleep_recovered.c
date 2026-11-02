/* Recovered from libbtctrl.a:ble_sleep.o. */
#include "bt_recovered_internal.h"
extern uint8_t *ble_con_show_of_beat;
extern uint32_t ble_evt_read_within_distribution(void);
uint32_t BT_SEC(".sleep_text.bb.ble") bb_get_at_attitude(uint32_t *duration, uint32_t margin)
{
    uint8_t *context = ble_con_show_of_beat;
    if (!context) return 1;
    if (context[74]) return 0;
    uint32_t now = ble_evt_read_within_distribution();
    uint32_t target = (BT_W(context, 32) - 8) & 0x07ffffff;
    if (((now - target) & 0x07ffffff) <= 0x0030bb00) return 0;
    uint32_t remaining = (target - now) & 0x07ffffff;
    if (margin >= remaining) return 0;
    if (remaining < *duration) *duration = remaining;
    return 1;
}
