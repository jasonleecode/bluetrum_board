/* Recovered controller hardware bring-up. All controller calls belong to
 * one thread; this HCI diagnostic is replaced by the NimBLE transport later. */
#include <rtthread.h>
#include <rthw.h>
#include "drv_bluetooth.h"
#include "btctrl_hal.h"
#include "bt_core_recovered.h"
#include "drv_btctrl_port.h"
#ifdef BSP_USING_FMRX
#include "../audio/drv_fmrx.h"
#endif

static struct rt_semaphore wake;
static rt_thread_t worker;
#ifdef BSP_USING_POWER
rt_bool_t ab32_btctrl_power_busy(void)
{
    /* Controller/RF cannot currently be released without a reboot. */
    return worker != RT_NULL;
}
#endif
#ifdef BSP_USING_NIMBLE
static drv_bt_hci_rx_cb_t host_receive;
static struct rt_semaphore started;
struct host_job { void (*run)(void *); void *arg; };
static rt_mq_t host_jobs;
#endif
static volatile rt_uint32_t irq_count, loops, events;
static volatile rt_uint32_t classic_irqs, le_irqs, radio_irqs;
static rt_uint32_t last_classic, last_le;
static volatile rt_uint32_t soft_kicks;
static volatile rt_bool_t irq_pending, soft_pending;
static volatile rt_bool_t ready, finished, matched;
static volatile rt_uint8_t request;
static rt_bool_t advertising;
static rt_bool_t scanning;
static uint32_t reports;
static uint32_t (*rom_conflict)(uint32_t);
static volatile uint32_t idle_classic_slots;
static rt_bool_t hci_passed;
static rt_uint16_t expected;
static rt_uint8_t result;
static const char *volatile stage = "idle";
/* Diagnostic placeholder, not an assigned public BD_ADDR. Advertising below
 * uses a static random address. Replace both identities for production. */
static uint8_t address[6] = {0x01, 0x00, 0x32, 0xab, 0x00, 0x02};
extern char __bt_start[], __bt_end[];
extern uint32_t ble_con_show_of_beat[];
extern void drv_bt_hci_rx_dispatch(uint8_t *, int);
#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
static uint32_t ble_radio_conflict(uint32_t slot)
{
    /* This port currently exposes BLE only. ROM's default conflict callback
     * reports busy for an empty classic slot (0x6000) and null descriptor.
     * 0x1042c/0x1042e are the two classic context counts, set by ROM entry
     * 0x845c0/0x845c4. Do not override arbitration for active classic links.
     * See docs/lib-analysis/BLUETOOTH_RECOVERY.md for the on-board A/B test. */
    if (!REG(0x1042c)) { ++idle_classic_slots; return 0; }
    return rom_conflict(slot);
}

extern void __real_ble_leave_about_television(uint32_t reset);
void __wrap_ble_leave_about_television(uint32_t reset)
{
    uintptr_t callbacks[7];
    __real_ble_leave_about_television(reset);
    /* Use the ROM registration API and retain the six other callbacks.
     * Reinstall on HCI Reset as well as cold init. */
    for (unsigned i = 0; i < 7; ++i) callbacks[i] = REG(0x10408 + 4 * i);
    rom_conflict = (uint32_t (*)(uint32_t))callbacks[6];
    callbacks[6] = (uintptr_t)ble_radio_conflict;
    ((void (*)(const uintptr_t *))(uintptr_t)0x84620)(callbacks);
}
extern void __real_bthw_isr_do(void);
void __wrap_bthw_isr_do(void)
{
    uint32_t classic = REG(0x8010), le = REG(0xf010);
    if (classic) { ++classic_irqs; last_classic = classic; }
    if (le) { ++le_irqs; last_le = le; }
    __real_bthw_isr_do();
}
void bb_rf_send_by_burn(uint32_t value) { (void)value; ++radio_irqs; }

static void diagnose(void)
{
    rt_kprintf("BT IRQ: classic=%u/%08x le=%u/%08x RF=%u\n",
               classic_irqs, last_classic, le_irqs, last_le, radio_irqs);
    rt_kprintf("BT HW: PIC=%08x/%08x BB=%08x/%08x/%08x LE=%08x/%08x/%08x\n",
               REG(0x544), REG(0x548), REG(0x8000), REG(0x800c), REG(0x8010),
               REG(0xf000), REG(0xf00c), REG(0xf010));
    rt_kprintf("BT LE: addr=%08x/%08x adv=%u scan=%u\n",
               REG(0xf024), REG(0xf028), advertising, scanning);
    rt_kprintf("BT scheduler: soft=%u time=%08x list=%08x/%08x group=%08x\n",
               soft_kicks, REG(0xf01c), ble_con_show_of_beat[0],
               ble_con_show_of_beat[1], ble_con_show_of_beat[2]);
    rt_kprintf("BT BLE-only idle classic slots=%u\n", idle_classic_slots);
    uintptr_t event = ble_con_show_of_beat[0];
    if (event >= (uintptr_t)__bt_start && event + 84 <= (uintptr_t)__bt_end)
        rt_kprintf("BT event: target=%08x interval=%u kind=%u active=%u flags=%02x\n",
                   REG(event + 32), *(uint16_t *)(event + 60),
                   *(uint8_t *)(event + 73), *(uint8_t *)(event + 74), *(uint8_t *)(event + 79));
}

void bt_get_local_bd_addr(uint8_t *p) { rt_memcpy(p, address, 6); }
void drv_vendor_set_bt_addr(const uint8_t *p) { rt_memcpy(address, p, 6); }
void nanos_event_set_trigger(void) { rt_sem_release(&wake); }
void bthw_soft_kick(void) { ++soft_kicks; soft_pending = RT_TRUE; rt_sem_release(&wake); }
void bthw_thread_post(void)
{
    ++irq_count;
    irq_pending = RT_TRUE;
    rt_sem_release(&wake);
}

static void pump(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    rt_bool_t irq = irq_pending, soft = soft_pending;
    irq_pending = soft_pending = RT_FALSE;
    rt_hw_interrupt_enable(level);
    if (irq) bthw_isr_do_plus();
    if (soft) bthw_soft_isr();
    bb_run_loop();
}
void hci_host_recv_packet(void *p, uint32_t n)
{
    drv_bt_hci_rx_dispatch(p, (int)n);
}

bool drv_bt_platform_init(void)
{
    void *heap;
    uint16_t *sizes;
    uint32_t blocks;
    if ((uintptr_t)bb_get_without_vehicle != 0x11000u ||
        (uintptr_t)__bt_end > 0x13800u) return false;
#ifdef BSP_USING_FMRX
    if (ab32_fmrx_bt_claim() != RT_EOK)
    {
        rt_kprintf("BT: stop FM first: fmrx stop\n");
        return false;
    }
#endif
    stage = "ROM heap";
    rt_memset(__bt_start, 0, __bt_end - __bt_start);
    /* Match the published BLE example's cold hardware reset before init. */
    stage = "controller cold reset";
    bb_off();
    bthw_get_heap_info(&heap, &sizes, &blocks);
    ((void (*)(void *, uint16_t *, uint32_t))(uintptr_t)0x84140)(heap, sizes, blocks);
    stage = "bb_init";
    rt_kprintf("BT: fixed RAM %p..%p, ROM heap ready\n", __bt_start, __bt_end);
    return true;
}

#define INIT_TRACE(name, label) \
    extern void __real_##name(void); \
    void __wrap_##name(void) { \
        stage = label; rt_kprintf("BT: %s begin\n", label); \
        __real_##name(); rt_kprintf("BT: %s done\n", label); }
INIT_TRACE(bb_make_into_mother, "RF init")
INIT_TRACE(bb_do_at_review, "classic init")
INIT_TRACE(bb_go_on_valuable, "LE init")

static void receive(drv_bt_hci_packet_type_t type, const uint8_t *p, uint16_t n)
{
    uint16_t i;
    ++events;
    if (type == DRV_BT_HCI_EVENT && n >= 4 && p[1] == 0x3e && p[3] == 2)
    { ++reports; return; }
    rt_kprintf("BT RX:");
    for (i = 0; i < n && i < 64; ++i) rt_kprintf(" %02x", p[i]);
    rt_kprintf("\n");
    if (type == DRV_BT_HCI_EVENT && n >= 7 && p[1] == 0x0e &&
        (uint16_t)(p[4] | ((uint16_t)p[5] << 8)) == expected)
    { result = p[6]; matched = RT_TRUE; }
}

static rt_bool_t command_data(uint16_t opcode, const char *name, const uint8_t *data, uint8_t size)
{
    rt_tick_t start = rt_tick_get();
    expected = opcode;
    matched = RT_FALSE;
    stage = name;
    rt_kprintf("BT TX: %s (0x%04x)\n", name, opcode);
    if (!drv_bt_hci_cmd(opcode, data, size)) return RT_FALSE;
    while (!matched && (rt_tick_t)(rt_tick_get() - start) < rt_tick_from_millisecond(2000))
    {
        pump();
        ++loops;
        rt_sem_take(&wake, 1);
    }
    if (!matched) rt_kprintf("BT FAIL: %s timeout\n", name);
    else rt_kprintf("BT %s: %s status=0x%02x\n", result ? "FAIL" : "PASS", name, result);
    return matched && !result;
}

static rt_bool_t command(uint16_t opcode, const char *name)
{
    return command_data(opcode, name, RT_NULL, 0);
}

static void advertise(rt_bool_t enable)
{
    uint8_t on = 0;
    /* Static random test address C2:00:AB:32:00:01; non-connectable, 100 ms,
     * all three advertising channels. No Host/GATT is implied by this test. */
    static const uint8_t random_addr[6] = {1, 0, 0x32, 0xab, 0, 0xc2};
    static const uint8_t params[15] = {0xa0, 0, 0xa0, 0, 3, 1, 0, 0, 0, 0, 0, 0, 0, 7, 0};
    static const uint8_t data[32] = {18, 2, 1, 6, 14, 9,
        'A','B','3','2','-','R','T','T','-','T','E','S','T'};
    if (scanning) { rt_kprintf("BT: stop scan first\n"); return; }
    if (advertising && !command_data(0x200a, "Advertising Disable", &on, 1)) goto failed;
    advertising = RT_FALSE;
    if (!enable) { stage = "advertising off"; return; }
    if (!command_data(0x2005, "Set Random Address", random_addr, sizeof(random_addr)) ||
        !command_data(0x2006, "Advertising Parameters", params, sizeof(params)) ||
        !command_data(0x2008, "Advertising Data", data, sizeof(data))) goto failed;
    on = 1;
    if (!command_data(0x200a, "Advertising Enable", &on, 1)) goto failed;
    advertising = RT_TRUE;
    stage = "advertising (await air capture)";
    rt_kprintf("BT: AB32-RTT-TEST advertising requested; verify with external scanner\n");
    return;
failed:
    stage = "advertising command failed";
}

static void scan(rt_bool_t enable)
{
    static const uint8_t params[7] = {0, 0x20, 0, 0x10, 0, 0, 0};
    static const uint8_t event_mask[8] = {0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x3f};
    uint8_t on[2] = {0, 0};
    if (advertising) advertise(RT_FALSE);
    if (advertising) return;
    if (scanning && !command_data(0x200c, "Scan Disable", on, 2)) goto failed;
    scanning = RT_FALSE;
    if (!enable) { stage = "scanning off"; return; }
    if (!command_data(0x0c01, "Set Event Mask", event_mask, sizeof(event_mask)) ||
        !command_data(0x200b, "Scan Parameters", params, sizeof(params))) goto failed;
    on[0] = 1;
    if (!command_data(0x200c, "Scan Enable", on, 2)) goto failed;
    scanning = RT_TRUE;
    stage = "scanning (count reports)";
    return;
failed:
    stage = "scanning command failed";
}

static void bt_worker_entry(void *unused)
{
    drv_bt_cfg_t cfg;
    rt_bool_t pass;
    (void)unused;
    rt_memcpy(cfg.public_addr, address, 6);
    cfg.hci_rx = receive;
#ifdef BSP_USING_NIMBLE
    if (host_receive) cfg.hci_rx = host_receive;
#endif
    ready = drv_bt_init(&cfg);
#ifdef BSP_USING_NIMBLE
    if (host_receive) rt_sem_release(&started);
#endif
    if (!ready)
    {
        stage = "platform rejected";
        finished = RT_TRUE;
        rt_kprintf("BT FAIL: platform prerequisites; reboot before retry\n");
        return;
    }
#ifdef BSP_USING_NIMBLE
    if (host_receive) {
        finished = RT_TRUE;
        stage = "NimBLE owns HCI";
        for (;;) {
            struct host_job job;
            /* Pump after each packet, so radio work cannot be starved. */
            if (rt_mq_recv(host_jobs, &job, sizeof(job), 0) == sizeof(job)) job.run(job.arg);
            pump(); ++loops;
            if (!host_jobs->entry) rt_sem_take(&wake, 1);
        }
    }
#endif
    pass = command(0x0c03, "HCI Reset") &&
           command(0x1001, "Read Local Version") &&
           command(0x1009, "Read BD_ADDR") &&
           command(0x2002, "LE Read Buffer Size");
    finished = RT_TRUE;
    hci_passed = pass;
    stage = pass ? "HCI passed" : "HCI failed";
    rt_kprintf("BT: %s; RF link/GATT not tested\n", stage);
    for (;;)
    {
        uint8_t action = request;
        if (action) {
            if (action <= 2) advertise(action == 1);
            else scan(action == 3);
            request = 0;
        }
        pump();
        ++loops;
        rt_sem_take(&wake, 1);
    }
}

#ifdef BSP_USING_NIMBLE
rt_bool_t ab32_btctrl_available(void) { return worker == RT_NULL; }
rt_err_t ab32_btctrl_start_host(drv_bt_hci_rx_cb_t receive_cb)
{
    if (worker || !receive_cb) return -RT_EBUSY;
#ifdef BSP_USING_FMRX
    rt_device_t fm = rt_device_find("fm0");
    if (fm && rt_device_control(fm, AB32_FMRX_STOP, RT_NULL) != RT_EOK) return -RT_EBUSY;
#endif
    rt_sem_init(&wake, "btwake", 0, RT_IPC_FLAG_PRIO);
    rt_sem_init(&started, "btrdy", 0, RT_IPC_FLAG_PRIO);
    host_jobs = rt_mq_create("bthci", sizeof(struct host_job), 8, RT_IPC_FLAG_FIFO);
    if (!host_jobs) { rt_sem_detach(&started); rt_sem_detach(&wake); return -RT_ENOMEM; }
    host_receive = receive_cb;
    worker = rt_thread_create("btctrl", bt_worker_entry, RT_NULL, 2048, 5, 1);
    if (!worker) {
        host_receive = RT_NULL; rt_mq_delete(host_jobs); host_jobs = RT_NULL;
        rt_sem_detach(&started); rt_sem_detach(&wake); return -RT_ENOMEM;
    }
    rt_thread_startup(worker);
    if (rt_sem_take(&started, rt_tick_from_millisecond(5000)) != RT_EOK) return -RT_ETIMEOUT;
    return ready ? RT_EOK : -RT_ERROR;
}
rt_err_t ab32_btctrl_submit(void (*run)(void *), void *arg)
{
    struct host_job job = {run, arg};
    if (!host_receive || !ready) return -RT_EIO;
    rt_err_t rc = rt_mq_send(host_jobs, &job, sizeof(job));
    if (rc == RT_EOK) rt_sem_release(&wake);
    return rc;
}
#endif

static void btctl(int argc, char **argv)
{
    if (argc == 2 && !rt_strcmp(argv[1], "test"))
    {
        if (worker) { rt_kprintf("BT: already started; use status or reboot\n"); return; }
#ifdef BSP_USING_FMRX
        rt_device_t fm = rt_device_find("fm0");
        if (fm && rt_device_control(fm, AB32_FMRX_STOP, RT_NULL) != RT_EOK)
        { rt_kprintf("BT: could not stop FM\n"); return; }
#endif
        rt_sem_init(&wake, "btwake", 0, RT_IPC_FLAG_PRIO);
        worker = rt_thread_create("btctrl", bt_worker_entry, RT_NULL, 2048, 5, 1);
        if (!worker) { rt_sem_detach(&wake); rt_kprintf("BT: no memory\n"); return; }
        stage = "starting";
        rt_thread_startup(worker);
    }
    else if (argc == 2 && (!rt_strcmp(argv[1], "adv") || !rt_strcmp(argv[1], "advoff") ||
                          !rt_strcmp(argv[1], "scan") || !rt_strcmp(argv[1], "scanoff")))
    {
        if (!ready || !finished || !hci_passed || request)
        { rt_kprintf("BT: run test first, or wait for current command\n"); return; }
        request = !rt_strcmp(argv[1], "adv") ? 1 : !rt_strcmp(argv[1], "advoff") ? 2 :
                  !rt_strcmp(argv[1], "scan") ? 3 : 4;
        rt_sem_release(&wake);
    }
    else if (argc == 2 && !rt_strcmp(argv[1], "status"))
    {
        rt_kprintf("BT: %s ready=%u finished=%u irq=%u loops=%u events=%u\n",
                   stage, ready, finished, irq_count, loops, events);
        if (ready) diagnose();
        rt_kprintf("BT scan reports=%u\n", reports);
    }
    else rt_kprintf("btctl test | status | adv | advoff | scan | scanoff (reboot to release RF)\n");
}
MSH_CMD_EXPORT(btctl, Recovered Bluetooth HCI hardware diagnostics);
