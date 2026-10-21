#include "drv_bluetooth.h"
#include "btctrl_hal.h"

static drv_bt_hci_rx_cb_t bt_hci_rx_cb;
static bool bt_ready;
static volatile bool bt_irq_pending;

static bool hci_packet_valid(const uint8_t *buf, int len)
{
    if (!buf || len <= 0 || (uint32_t)len > UINT16_MAX) return false;
    if (buf[0] == DRV_BT_HCI_EVENT)
        return len >= 3 && len == 3 + buf[2];
    if (buf[0] == DRV_BT_HCI_ACL)
        return len >= 5 && (uint32_t)len == 5u + (buf[3] | ((uint32_t)buf[4] << 8));
    return false;
}

void drv_bt_hci_rx_dispatch(uint8_t *buf, int len)
{
    if (bt_hci_rx_cb && hci_packet_valid(buf, len)) {
        bt_hci_rx_cb((drv_bt_hci_packet_type_t)buf[0], buf, (uint16_t)len);
    }
    /* Controller owns the buffer. Even discarded packets need completion. */
    hct_tx_done();
}

bool drv_bt_init(const drv_bt_cfg_t *cfg)
{
    if (bt_ready || !drv_bt_platform_init()) return false;

    bt_irq_pending = false;
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

    if (bt_irq_pending) {
        bt_irq_pending = false;
        bthw_isr_do_plus(); /* Process and re-enable the masked controller IRQ. */
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
        bt_irq_pending = false;
        bt_hci_rx_cb = 0;
    }
}

void drv_bt_isr(void)
{
    /* bb_init enables IRQ sources before it returns. Retain a notification
     * from that window even though bt_ready has not yet been set. */
    bt_irq_pending = true;
}

bool drv_bt_hci_cmd(uint16_t opcode, const uint8_t *params, uint8_t len)
{
    /* This SDK dispatches OCF with opcode & 0xff, dropping bits 8 and 9. */
    if (!bt_ready || (opcode & 0x0300u) || (!params && len != 0)) {
        return false;
    }

    hct_send_command(opcode, len, (uint8_t *)params);
    return true;
}

bool drv_bt_hci_acl(uint16_t handle, uint8_t flags, const uint8_t *data, uint16_t len)
{
    if (!bt_ready || handle > 0x0fffu || flags > 0x0fu || (!data && len != 0)) {
        return false;
    }

    /* hct_acl_segment copies only one fragment, silently truncating longer
     * payloads. Check the same capacity before handing it any data. */
    uint16_t capacity = (flags <= 3u && bb_utils_keep_of_age(handle) == 4u)
        ? 27u : 679u;
    if (len > capacity) return false;
    return hct_acl_segment(handle, flags, len, (uint8_t *)data);
}

void drv_bt_get_local_addr(uint8_t addr[6])
{
    if (addr) {
        bt_get_local_bd_addr(addr);
    }
}
