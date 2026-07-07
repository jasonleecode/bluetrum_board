#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    DRV_BT_HCI_EVENT = 0x04,
    DRV_BT_HCI_ACL = 0x02,
} drv_bt_hci_packet_type_t;

typedef void (*drv_bt_hci_rx_cb_t)(drv_bt_hci_packet_type_t type,
                                   const uint8_t *buf,
                                   uint16_t len);

typedef struct {
    uint8_t public_addr[6];
    drv_bt_hci_rx_cb_t hci_rx;
} drv_bt_cfg_t;

bool drv_bt_init(const drv_bt_cfg_t *cfg);
void drv_bt_run_loop(void);
void drv_bt_sleep(void);
void drv_bt_power_off(void);
void drv_bt_isr(void);
bool drv_bt_hci_cmd(uint16_t opcode, const uint8_t *params, uint8_t len);
bool drv_bt_hci_acl(uint16_t handle, uint8_t flags, const uint8_t *data, uint16_t len);
void drv_bt_get_local_addr(uint8_t addr[6]);
