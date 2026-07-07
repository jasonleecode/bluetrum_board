#include "drv_bluetooth.h"

extern void bb_init(void);
extern void bb_run_loop(void);
extern void bb_sleep(void);
extern void bb_off(void);
extern void bthw_irq_init(void);
extern void bthw_isr_do(void);
extern void bthw_soft_isr(void);
extern void hct_send_command(uint16_t opcode, uint8_t len, uint8_t pbuf[]);
extern bool hct_acl_segment(uint16_t handle, uint8_t flags, uint16_t len, uint8_t pbuf[]);
extern void hct_tx_done(void);
extern void bt_get_local_bd_addr(uint8_t *addr);
extern void drv_vendor_set_bt_addr(const uint8_t *addr);

static drv_bt_hci_rx_cb_t bt_hci_rx_cb;
static bool bt_ready;

void drv_bt_hci_rx_dispatch(uint8_t *buf, int len)
{
    drv_bt_hci_packet_type_t type;

    if (!bt_hci_rx_cb || !buf || len <= 0) {
        return;
    }

    type = (buf[0] == DRV_BT_HCI_ACL) ? DRV_BT_HCI_ACL : DRV_BT_HCI_EVENT;
    bt_hci_rx_cb(type, buf, (uint16_t)len);
    hct_tx_done();
}

bool drv_bt_init(const drv_bt_cfg_t *cfg)
{
    bt_hci_rx_cb = cfg ? cfg->hci_rx : 0;
    if (cfg) {
        drv_vendor_set_bt_addr(cfg->public_addr);
    }
    bthw_irq_init();
    bb_init();
    bt_ready = true;

    return true;
}

void drv_bt_run_loop(void)
{
    if (!bt_ready) {
        return;
    }

    bthw_soft_isr();
    bb_run_loop();
}

void drv_bt_sleep(void)
{
    if (bt_ready) {
        bb_sleep();
    }
}

void drv_bt_power_off(void)
{
    if (bt_ready) {
        bb_off();
        bt_ready = false;
    }
}

void drv_bt_isr(void)
{
    if (bt_ready) {
        bthw_isr_do();
    }
}

bool drv_bt_hci_cmd(uint16_t opcode, const uint8_t *params, uint8_t len)
{
    if (!bt_ready || (!params && len != 0)) {
        return false;
    }

    hct_send_command(opcode, len, (uint8_t *)params);
    return true;
}

bool drv_bt_hci_acl(uint16_t handle, uint8_t flags, const uint8_t *data, uint16_t len)
{
    if (!bt_ready || (!data && len != 0)) {
        return false;
    }

    return hct_acl_segment(handle, flags, len, (uint8_t *)data);
}

void drv_bt_get_local_addr(uint8_t addr[6])
{
    if (addr) {
        bt_get_local_bd_addr(addr);
    }
}
