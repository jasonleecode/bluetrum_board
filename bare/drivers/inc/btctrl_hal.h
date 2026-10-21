#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Interfaces used by the bare wrapper, checked against libbtctrl.a.
 * This is a controller ABI, not a GAP/GATT or Bluetooth host API.
 * See docs/lib-analysis/BLUETOOTH_RECOVERY.md for ROM/platform dependencies.
 */
void bb_init(void);
void bb_run_loop(void);
void bb_sleep(void);
void bb_off(void);
void bthw_irq_init(void);
void bthw_isr_do_plus(void);
void bthw_soft_isr(void);
void bthw_get_heap_info(void **heap, uint16_t **sizes, uint32_t *block_count);
void hct_send_command(uint16_t opcode, uint8_t length, uint8_t *parameters);
bool hct_acl_segment(uint16_t handle, uint8_t flags, uint16_t length, uint8_t *data);
void hct_tx_done(void);
uint32_t bb_utils_keep_of_age(uint16_t handle);
void bt_get_local_bd_addr(uint8_t *address);
void drv_vendor_set_bt_addr(const uint8_t *address);
