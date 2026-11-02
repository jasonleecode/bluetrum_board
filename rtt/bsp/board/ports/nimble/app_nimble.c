/* SPDX-License-Identifier: Apache-2.0 */
#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "../bluetooth/drv_btctrl_port.h"
extern void ab32_nimble_receive(drv_bt_hci_packet_type_t, const uint8_t *, uint16_t);
extern void ab32_nimble_transport_status(void);
static rt_thread_t host_thread;
static volatile bool synced;
static volatile uint16_t connection = BLE_HS_CONN_HANDLE_NONE;
static uint16_t value_handle;
static uint8_t value[64] = "AB32 RTT NimBLE";
static uint16_t value_len = 15;
static uint32_t reads, writes, connects, disconnects, resets;
static int last_error;
static struct ble_npl_event control_event;
static uint8_t action;
/* ab320001-5e7f-4c9a-9b7b-75ba2a2f0001 / ...0002 */
static const ble_uuid128_t service_uuid = BLE_UUID128_INIT(
    1,0,0x2f,0x2a,0xba,0x75,0x7b,0x9b,0x9a,0x4c,0x7f,0x5e,1,0,0x32,0xab);
static const ble_uuid128_t value_uuid = BLE_UUID128_INIT(
    2,0,0x2f,0x2a,0xba,0x75,0x7b,0x9b,0x9a,0x4c,0x7f,0x5e,1,0,0x32,0xab);
static int access_value(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctx, void *arg)
{
    if (ctx->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        ++reads;
        return os_mbuf_append(ctx->om, value, value_len) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
    }
    if (ctx->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        unsigned len = OS_MBUF_PKTLEN(ctx->om);
        if (len > sizeof(value)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if (os_mbuf_copydata(ctx->om, 0, len, value)) return BLE_ATT_ERR_UNLIKELY;
        value_len = len; ++writes;
        ble_gatts_chr_updated(value_handle);
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}
static const struct ble_gatt_chr_def characteristics[] = {
    {.uuid = &value_uuid.u, .access_cb = access_value,
     .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
     .val_handle = &value_handle}, {0}
};
static const struct ble_gatt_svc_def services[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &service_uuid.u, .characteristics = characteristics}, {0}
};
static int gap_event(struct ble_gap_event *event, void *arg);
static void advertise(void)
{
    struct ble_hs_adv_fields fields = {0};
    struct ble_gap_adv_params params = {0};
    const char *name = ble_svc_gap_device_name();
    if (!synced || connection != BLE_HS_CONN_HANDLE_NONE || ble_gap_adv_active()) return;
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)name; fields.name_len = strlen(name); fields.name_is_complete = 1;
    last_error = ble_gap_adv_set_fields(&fields);
    if (!last_error) {
        memset(&fields, 0, sizeof(fields)); fields.uuids128 = (ble_uuid128_t *)&service_uuid;
        fields.num_uuids128 = 1; fields.uuids128_is_complete = 1;
        last_error = ble_gap_adv_rsp_set_fields(&fields);
    }
    params.conn_mode = BLE_GAP_CONN_MODE_UND; params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min = params.itvl_max = 160;
    if (!last_error) last_error = ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, BLE_HS_FOREVER,
                                                   &params, gap_event, NULL);
    rt_kprintf("BLE advertising: %d name=AB32-NIMBLE\n", last_error);
}
static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        last_error = event->connect.status;
        if (!last_error) { connection = event->connect.conn_handle; ++connects; }
        else advertise();
        rt_kprintf("BLE connect: status=%d handle=%u\n", last_error, connection); break;
    case BLE_GAP_EVENT_DISCONNECT:
        connection = BLE_HS_CONN_HANDLE_NONE; ++disconnects;
        rt_kprintf("BLE disconnect: reason=%d\n", event->disconnect.reason);
        advertise(); break;
    case BLE_GAP_EVENT_MTU:
        rt_kprintf("BLE MTU: %u\n", event->mtu.value); break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise(); break;
    default: break;
    }
    return 0;
}
static void on_sync(void)
{
    static const uint8_t addr[6] = {2, 0, 0x32, 0xab, 0, 0xc2};
    last_error = ble_hs_id_set_rnd(addr);
    synced = last_error == 0;
    rt_kprintf("BLE sync: %d, static address C2:00:AB:32:00:02\n", last_error);
    advertise();
}
static void on_reset(int reason)
{
    synced = false; connection = BLE_HS_CONN_HANDLE_NONE; ++resets; last_error = reason;
    rt_kprintf("BLE reset: %d\n", reason);
}
static void control(struct ble_npl_event *ev)
{
    uint8_t request = action; action = 0;
    if (request == 1) advertise();
    else if (request == 2) last_error = ble_gap_adv_stop();
}
static void host_entry(void *arg)
{
    nimble_port_init();
    ble_npl_event_init(&control_event, control, NULL);
    ble_hs_cfg.sync_cb = on_sync; ble_hs_cfg.reset_cb = on_reset;
    ble_svc_gap_init(); ble_svc_gatt_init();
    ble_svc_gap_device_name_set("AB32-NIMBLE");
    last_error = ble_gatts_count_cfg(services);
    if (!last_error) last_error = ble_gatts_add_svcs(services);
    if (!last_error) last_error = ab32_btctrl_start_host(ab32_nimble_receive);
    if (last_error) { rt_kprintf("BLE init failed: %d; reboot to retry\n", last_error); return; }
    rt_kprintf("BLE Host running (one connection, MTU 64, unencrypted test service)\n");
    nimble_port_run();
}
static void ble(int argc, char **argv)
{
    if (argc == 2 && !rt_strcmp(argv[1], "start")) {
        if (host_thread || !ab32_btctrl_available()) { rt_kprintf("BLE: controller already started; use status or reboot\n"); return; }
        host_thread = rt_thread_create("nimble", host_entry, NULL, 3072, 9, 5);
        if (host_thread) rt_thread_startup(host_thread);
        else rt_kprintf("BLE: no memory\n");
    } else if (argc == 2 && !rt_strcmp(argv[1], "status")) {
        rt_kprintf("BLE synced=%u conn=%u error=%d connections=%u disconnects=%u resets=%u\n",
                   synced, connection, last_error, connects, disconnects, resets);
        rt_kprintf("GATT reads=%u writes=%u bytes=%u value_handle=%u\n", reads, writes, value_len, value_handle);
        ab32_nimble_transport_status();
    } else if (argc == 2 && (!rt_strcmp(argv[1], "adv") || !rt_strcmp(argv[1], "stop"))) {
        if (!synced || action) { rt_kprintf("BLE: not ready or busy\n"); return; }
        action = !rt_strcmp(argv[1], "adv") ? 1 : 2;
        ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &control_event);
    } else rt_kprintf("ble start | status | adv | stop (stop advertising; reboot releases RF)\n");
}
MSH_CMD_EXPORT(ble, NimBLE peripheral and read/write/notify test service);
