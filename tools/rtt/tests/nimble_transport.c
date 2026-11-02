/* Ownership, bounds and backpressure in the actual controller transport. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../rtt/bsp/board/ports/nimble/transport_ab32.c"
struct packet { struct os_mbuf om; struct os_mbuf_pkthdr hdr; uint8_t data[64]; };
static struct packet incoming;
static int freed, faults, event_freed, submitted, reject, no_events, no_acl;
static uint8_t event_storage[257], *received_event, sent_payload[27];
static struct { void (*fn)(void *); void *arg; } jobs[8];
void rt_assert_handler(const char *e, const char *f, rt_size_t l) { fprintf(stderr, "%s:%lu %s\n", f, (unsigned long)l, e); abort(); }
int rt_kprintf(const char *fmt, ...) { return 0; }
uint32_t ble_npl_hw_enter_critical(void) { return 0; }
void ble_npl_hw_exit_critical(uint32_t x) { }
void ble_hs_sched_reset(int reason) { assert(reason == BLE_HS_ECONTROLLER); ++faults; }
rt_err_t ab32_btctrl_submit(void (*fn)(void *), void *arg)
{ if (reject) return -RT_EFULL; assert(submitted < 8); jobs[submitted].fn = fn; jobs[submitted++].arg = arg; return RT_EOK; }
bool drv_bt_hci_cmd(uint16_t op, const uint8_t *p, uint8_t n) { assert(op == 0xc03 && n == 0); return true; }
bool drv_bt_hci_acl(uint16_t h, uint8_t f, const uint8_t *p, uint16_t n)
{ assert(h == 0 && f == 2 && n == 27); memcpy(sent_payload, p, n); return true; }
int os_mbuf_free_chain(struct os_mbuf *om) { ++freed; return 0; }
int os_mbuf_copydata(const struct os_mbuf *om, int off, int len, void *dst)
{ assert(off + len <= OS_MBUF_PKTLEN(om)); memcpy(dst, om->om_data + off, len); return 0; }
int os_mbuf_append(struct os_mbuf *om, const void *data, uint16_t len)
{ assert(len <= 31); memcpy(om->om_data, data, len); OS_MBUF_PKTLEN(om) = len; return 0; }
void *ble_transport_alloc_evt(int discardable) { return no_events ? NULL : event_storage; }
void ble_transport_free(void *buf) { ++event_freed; }
struct os_mbuf *ble_transport_alloc_acl_from_ll(void) { incoming.om.om_data = incoming.data; return no_acl ? NULL : &incoming.om; }
int ble_transport_to_hs_evt_impl(void *p) { received_event = p; return 0; }
int ble_transport_to_hs_acl_impl(struct os_mbuf *om) { assert(om == &incoming.om); return 0; }
static void flush(void)
{ for (int i = 0; i < submitted; ++i) jobs[i].fn(jobs[i].arg); submitted = 0; }
int main(void)
{
    uint8_t command[] = {3, 12, 0};
    assert(!ble_transport_to_ll_cmd_impl(command) && !event_freed);
    flush(); assert(event_freed == 1);
    reject = 1;
    assert(ble_transport_to_ll_cmd_impl(command) == BLE_HS_EOS && event_freed == 2);
    reject = 0;
    struct packet tx = {0}; tx.om.om_data = tx.data;
    OS_MBUF_PKTLEN(&tx.om) = 31;
    tx.data[1] = 0x20; tx.data[2] = 27;
    memset(tx.data + 4, 0x5a, 27);
    for (int i = 0; i < 6; ++i) assert(!ble_transport_to_ll_acl_impl(&tx.om));
    assert(freed == 6); /* Host's fragmentation mbuf released immediately. */
    assert(ble_transport_to_ll_acl_impl(&tx.om) == BLE_HS_ENOMEM && freed == 7);
    memset(tx.data + 4, 0xa5, 27);
    flush(); assert(sent_payload[0] == 0x5a && sent_payload[26] == 0x5a);
    for (unsigned i = 0; i < 6; ++i) assert(!acl_tx[i].used);
    reject = 1; assert(ble_transport_to_ll_acl_impl(&tx.om) == BLE_HS_EOS);
    for (unsigned i = 0; i < 6; ++i) assert(!acl_tx[i].used);
    reject = 0; OS_MBUF_PKTLEN(&tx.om) = 32;
    assert(ble_transport_to_ll_acl_impl(&tx.om) == BLE_HS_EMSGSIZE);
    uint8_t event[] = {4, 0x0e, 4, 5, 3, 12, 0};
    ab32_nimble_receive(DRV_BT_HCI_EVENT, event, sizeof(event));
    event[4] = 0xff; assert(received_event[3] == 3); /* borrowed RX was copied */
    int before = faults;
    no_events = 1;
    ab32_nimble_receive(DRV_BT_HCI_EVENT, event, sizeof(event));
    assert(faults == before + 1);
    uint8_t report[] = {4, 0x3e, 1, 2};
    ab32_nimble_receive(DRV_BT_HCI_EVENT, report, sizeof(report));
    assert(faults == before + 1); /* discardable pool exhaustion */
    uint8_t malformed[] = {4, 0x0e, 0};
    ab32_nimble_receive(DRV_BT_HCI_EVENT, malformed, sizeof(malformed));
    assert(faults == before + 2);
    uint8_t acl[] = {2, 0, 0x20, 3, 0, 1, 2, 3};
    ab32_nimble_receive(DRV_BT_HCI_ACL, acl, sizeof(acl));
    acl[5] = 0xff; assert(incoming.data[4] == 1);
    no_acl = 1;
    ab32_nimble_receive(DRV_BT_HCI_ACL, acl, sizeof(acl));
    assert(faults == before + 3);
    puts("PASS: HCI copied RX, deferred TX, ACL bounds, fragment release, queue/pool exhaustion");
}
