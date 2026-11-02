/* SPDX-License-Identifier: Apache-2.0
 * Controller buffers are borrowed only for the RX callback. Copy before ack.
 * All controller APIs run on btctrl; NimBLE owns fragmentation and credits.
 */
#include "os/os.h"
#include "host/ble_hs.h"
#include "nimble/transport.h"
#include "../bluetooth/drv_btctrl_port.h"
static struct { uint8_t data[32]; bool used; } acl_tx[6];
static uint32_t tx_commands, tx_acl, rx_events, rx_acl, errors;
static void fault(void) { ++errors; ble_hs_sched_reset(BLE_HS_ECONTROLLER); }
static void send_command(void *buf)
{
    uint8_t *p = buf;
    bool ok = drv_bt_hci_cmd(p[0] | (p[1] << 8), p + 3, p[2]);
    ++tx_commands;
    ble_transport_free(buf);
    if (!ok) fault();
}
int ble_transport_to_ll_cmd_impl(void *buf)
{
    if (ab32_btctrl_submit(send_command, buf) == RT_EOK) return 0;
    ble_transport_free(buf); ++errors; return BLE_HS_EOS;
}
rt_section(".com_text.nimble")
static __attribute__((noinline)) void free_acl(unsigned slot)
{
    uint32_t level = ble_npl_hw_enter_critical();
    acl_tx[slot].used = false;
    ble_npl_hw_exit_critical(level);
}
rt_section(".com_text.nimble")
static __attribute__((noinline)) unsigned alloc_acl(void)
{
    unsigned slot;
    uint32_t level = ble_npl_hw_enter_critical();
    for (slot = 0; slot < 6 && acl_tx[slot].used; ++slot) {}
    if (slot < 6) acl_tx[slot].used = true;
    ble_npl_hw_exit_critical(level);
    return slot;
}
static void send_acl(void *arg)
{
    unsigned slot = (uintptr_t)arg;
    uint8_t *p = acl_tx[slot].data;
    bool ok = drv_bt_hci_acl(p[0] | ((p[1] & 15) << 8), p[1] >> 4,
                              p + 4, p[2] | (p[3] << 8));
    ++tx_acl; free_acl(slot);
    if (!ok) fault();
}
int ble_transport_to_ll_acl_impl(struct os_mbuf *om)
{
    unsigned slot;
    uint16_t n = OS_MBUF_PKTLEN(om);
    int rc = BLE_HS_EMSGSIZE;
    if (n < 4 || n > 31) goto done;
    slot = alloc_acl();
    rc = BLE_HS_ENOMEM;
    if (slot == 6) goto done;
    rc = os_mbuf_copydata(om, 0, n, acl_tx[slot].data);
    uint8_t *p = acl_tx[slot].data;
    if (!rc && (p[2] | (p[3] << 8)) != n - 4) rc = BLE_HS_EBADDATA;
    if (!rc && ab32_btctrl_submit(send_acl, (void *)(uintptr_t)slot) != RT_EOK) rc = BLE_HS_EOS;
    if (rc) free_acl(slot);
done:
    /* Release even on error; in particular don't retain the Host's single
     * fragmentation mbuf while waiting for the controller worker. */
    os_mbuf_free_chain(om);
    if (rc) ++errors;
    return rc;
}
int ble_transport_to_ll_iso_impl(struct os_mbuf *om)
{ os_mbuf_free_chain(om); return BLE_HS_ENOTSUP; }
void ble_transport_ll_init(void) { }
void ab32_nimble_receive(drv_bt_hci_packet_type_t type, const uint8_t *h4, uint16_t n)
{
    if (type == DRV_BT_HCI_EVENT) {
        if (n < 3 || n != h4[2] + 3 || n - 1 > MYNEWT_VAL(BLE_TRANSPORT_EVT_SIZE)) { fault(); return; }
        if ((h4[1] == 0x0e && h4[2] < 3) || (h4[1] == 0x0f && h4[2] < 4) ||
            (h4[1] == 0x3e && !h4[2])) { fault(); return; }
        bool discardable = n >= 4 && h4[1] == 0x3e && h4[3] == 2;
        uint8_t *p = ble_transport_alloc_evt(discardable);
        if (!p) { if (!discardable) fault(); return; }
        memcpy(p, h4 + 1, n - 1); ++rx_events;
        ble_transport_to_hs_evt(p); /* Host consumes the copy. */
    } else if (type == DRV_BT_HCI_ACL) {
        if (n < 5 || n != 5 + (h4[3] | (h4[4] << 8)) || n > 32) { fault(); return; }
        struct os_mbuf *om = ble_transport_alloc_acl_from_ll();
        if (!om) { fault(); return; }
        if (os_mbuf_append(om, h4 + 1, n - 1)) { os_mbuf_free_chain(om); fault(); return; }
        ++rx_acl;
        ble_transport_to_hs_acl(om); /* Consumes on both success and failure. */
    }
}
void ab32_nimble_transport_status(void)
{
    rt_kprintf("HCI cmd=%u tx_acl=%u events=%u rx_acl=%u errors=%u\n",
               tx_commands, tx_acl, rx_events, rx_acl, errors);
}
