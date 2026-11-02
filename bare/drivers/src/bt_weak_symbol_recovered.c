/* Recovered weak defaults from libbtctrl.a:weak_symbol.o. */
#include "bt_recovered_internal.h"
extern void bt_get_local_bd_addr(uint8_t *);
uint32_t BT_WEAK bb_rf_get_about_importance(void) { return 0; }
uint32_t BT_WEAK weak_symbol_give_to_preference(void) { return 0; }
void BT_WEAK ble_get_local_bd_addr(uint8_t *address)
{ bt_get_local_bd_addr(address); address[5] ^= 0x55; }
uint32_t BT_WEAK mgr_fetch_on_savings(void) { return 0; }
