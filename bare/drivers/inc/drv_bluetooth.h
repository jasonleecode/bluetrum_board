#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    DRV_BT_HCI_EVENT = 0x04,
    DRV_BT_HCI_ACL = 0x02,
} drv_bt_hci_packet_type_t;

/* buf includes the H4 packet-type byte. Borrowed until the callback returns;
 * copy it before retaining it. Do not re-enter controller APIs from here.
 */
typedef void (*drv_bt_hci_rx_cb_t)(drv_bt_hci_packet_type_t type,
                                   const uint8_t *buf,
                                   uint16_t len);

typedef struct {
    uint8_t public_addr[6];
    drv_bt_hci_rx_cb_t hci_rx;
} drv_bt_cfg_t;

/* Board port hook: prepare clocks, fixed BT RAM/ROM heap, real interrupt
 * vectors/dispatch and wakeup hooks. The supplied weak stub returns false.
 * A true result must mean all prerequisites are ready, not merely allocated.
 */
bool drv_bt_platform_init(void);

/* Fails if the platform hook fails or the controller was already initialized.
 * Success means the initialization calls returned, not a working radio link.
 */
bool drv_bt_init(const drv_bt_cfg_t *cfg);
/* Poll from the main loop, not from an IRQ or HCI receive callback. */
void drv_bt_run_loop(void);
void drv_bt_sleep(void);
void drv_bt_power_off(void);
/* Schedule deferred IRQ work. bthw_thread_post() normally calls this after
 * the vendor IRQ top half masks the source; the next poll processes/rearms it.
 */
void drv_bt_isr(void);
/* true means accepted by this wrapper, not queued/transmitted successfully.
 * Command credit must be managed by the host; OCF > 255 is rejected because
 * this vendor dispatch table ignores OCF bits 8..9.
 */
bool drv_bt_hci_cmd(uint16_t opcode, const uint8_t *params, uint8_t len);
/* One fragment only: reject payloads above the vendor path capacity (27 or
 * 679 bytes). handle is 12 bits, flags is the raw HCI header flags nibble.
 * Vendor true does not reliably report allocation/queue success; host flow
 * control and fragmentation are still required.
 */
bool drv_bt_hci_acl(uint16_t handle, uint8_t flags, const uint8_t *data, uint16_t len);
void drv_bt_get_local_addr(uint8_t addr[6]);
