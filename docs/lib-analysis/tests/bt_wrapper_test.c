#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include "drv_bluetooth.h"
#include "btctrl_hal.h"

void drv_bt_hci_rx_dispatch(uint8_t *buf, int len);
static unsigned hardware_calls, acknowledgements;
#ifndef USE_VENDOR_SHIM
static unsigned platform_calls, deliveries;
static bool platform_ready;
static bool interrupt_during_init;
#endif
static unsigned irq_work, polls, soft_polls, commands, acl_calls;
static bool acl_result;
static uint32_t link_type = 4;
static uint16_t last_opcode, last_handle, last_length;
static uint8_t last_flags;
static const uint8_t *last_data;

#ifndef USE_VENDOR_SHIM
bool drv_bt_platform_init(void) { ++platform_calls; return platform_ready; }
void drv_vendor_set_bt_addr(const uint8_t *a) { assert(a); ++hardware_calls; }
void bt_get_local_bd_addr(uint8_t *a) { a[0] = 0x41; }
#endif
void bthw_irq_init(void) { ++hardware_calls; }
void bb_init(void)
{
    ++hardware_calls;
#ifndef USE_VENDOR_SHIM
    if (interrupt_during_init) drv_bt_isr();
#endif
}
void bb_off(void) { ++hardware_calls; }
void bb_sleep(void) { ++hardware_calls; }
void bthw_isr_do_plus(void) { ++irq_work; }
void bthw_soft_isr(void) { ++soft_polls; }
void bb_run_loop(void) { ++polls; }
void hct_tx_done(void) { ++acknowledgements; }
uint32_t bb_utils_keep_of_age(uint16_t handle) { (void)handle; return link_type; }
void hct_send_command(uint16_t op, uint8_t len, uint8_t *p)
{
    ++commands; last_opcode = op; last_length = len; last_data = p;
}
bool hct_acl_segment(uint16_t h, uint8_t f, uint16_t n, uint8_t *p)
{
    ++acl_calls; last_handle = h; last_flags = f; last_length = n; last_data = p;
    return acl_result;
}

#ifndef USE_VENDOR_SHIM
static void receive(drv_bt_hci_packet_type_t type, const uint8_t *buf, uint16_t len)
{
    assert(buf == last_data && len == last_length && type == buf[0]);
    /* Completion must occur after the user has consumed the borrowed data. */
    assert(acknowledgements == deliveries);
    ++deliveries;
}
#endif

int main(void)
{
#ifdef USE_VENDOR_SHIM
    assert(!drv_bt_init(NULL));
    assert(hardware_calls == 0);
    puts("PASS default shim refuses Bluetooth initialization before hardware calls");
#else
    drv_bt_cfg_t cfg = {{0x41, 0x42, 0, 0, 0, 1}, receive};
    uint8_t payload[680] = {0};
    uint8_t event[] = {4, 0x0e, 4, 1, 3, 0x0c, 0};
    uint8_t acl[] = {2, 0, 0, 2, 0, 0xaa, 0xbb};
    assert(!drv_bt_init(&cfg));
    assert(platform_calls == 1 && hardware_calls == 0);
    assert(!drv_bt_hci_cmd(0x0c03, NULL, 0));
    assert(!drv_bt_hci_acl(0, 0, payload, 1));
    drv_bt_isr(); drv_bt_run_loop();
    assert(irq_work == 0 && polls == 0);

    platform_ready = true;
    assert(drv_bt_init(&cfg));
    assert(platform_calls == 2 && hardware_calls == 3);
    assert(!drv_bt_init(&cfg));
    assert(platform_calls == 2 && hardware_calls == 3);
    drv_bt_isr(); drv_bt_isr();
    assert(irq_work == 0); /* IRQ context only schedules work. */
    drv_bt_run_loop();
    assert(irq_work == 1 && polls == 1 && soft_polls == 1);
    drv_bt_run_loop();
    assert(irq_work == 1 && polls == 2 && soft_polls == 2);

    assert(drv_bt_hci_cmd(0x0c03, NULL, 0));
    assert(commands == 1 && last_opcode == 0x0c03 && last_length == 0);
    assert(drv_bt_hci_cmd(0x2006, payload, 15));
    assert(commands == 2 && last_opcode == 0x2006 && last_length == 15 && last_data == payload);
    assert(!drv_bt_hci_cmd(0x2106, payload, 15)); /* Would alias 0x2006. */
    assert(!drv_bt_hci_cmd(0x2206, payload, 15));
    assert(!drv_bt_hci_cmd(0x2006, NULL, 1));
    assert(commands == 2);

    acl_result = true;
    assert(!drv_bt_hci_acl(0, 0, NULL, 1));
    assert(!drv_bt_hci_acl(0x1000, 0, payload, 1));
    assert(!drv_bt_hci_acl(0, 16, payload, 1));
    assert(!drv_bt_hci_acl(0, 0, payload, 28));
    assert(acl_calls == 0);
    assert(drv_bt_hci_acl(0, 0, payload, 27));
    assert(last_handle == 0 && last_flags == 0 && last_length == 27 && last_data == payload);
    assert(drv_bt_hci_acl(0, 4, payload, 679));
    assert(!drv_bt_hci_acl(0, 4, payload, 680));
    link_type = 1;
    assert(drv_bt_hci_acl(128, 2, payload, 679));
    assert(last_handle == 128 && last_flags == 2 && last_length == 679);
    assert(!drv_bt_hci_acl(128, 2, payload, 680));
    acl_result = false;
    assert(!drv_bt_hci_acl(128, 2, payload, 1));
    assert(acl_calls == 4);

    last_data = event; last_length = sizeof(event);
    drv_bt_hci_rx_dispatch(event, sizeof(event));
    last_data = acl; last_length = sizeof(acl);
    drv_bt_hci_rx_dispatch(acl, sizeof(acl));
    assert(deliveries == 2 && acknowledgements == 2);
    drv_bt_hci_rx_dispatch(NULL, 7);
    drv_bt_hci_rx_dispatch(event, 0);
    drv_bt_hci_rx_dispatch(event, -1);
    drv_bt_hci_rx_dispatch(event, 65536);
    drv_bt_hci_rx_dispatch(event, 2);
    drv_bt_hci_rx_dispatch(event, sizeof(event) - 1);
    drv_bt_hci_rx_dispatch(acl, 4);
    drv_bt_hci_rx_dispatch(acl, sizeof(acl) - 1);
    event[0] = 3;
    drv_bt_hci_rx_dispatch(event, sizeof(event));
    assert(deliveries == 2 && acknowledgements == 11);

    drv_bt_isr();
    drv_bt_power_off();
    drv_bt_run_loop();
    assert(irq_work == 1 && polls == 2);
    assert(!drv_bt_hci_cmd(0x0c03, NULL, 0));
    interrupt_during_init = true;
    assert(drv_bt_init(NULL)); /* No consumer still completes controller RX. */
    drv_bt_run_loop();
    assert(irq_work == 2 && polls == 3); /* Do not lose IRQs raised by bb_init. */
    event[0] = 4;
    drv_bt_hci_rx_dispatch(event, sizeof(event));
    assert(deliveries == 2 && acknowledgements == 12);
    drv_bt_power_off();
    puts("PASS Bluetooth wrapper: readiness, deferred IRQ, H4 ownership and TX boundaries");
#endif
    return 0;
}
