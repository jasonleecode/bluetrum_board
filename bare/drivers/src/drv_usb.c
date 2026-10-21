#include "drv_usb.h"
#include "usb_hal.h"

static bool usb_ready;
static bool hid_pending;
static bool hid_started;
static bool hid_needs_restart;
static uint8_t hid_report;
static usb_ep_state_t *hid_endpoint;

bool drv_usb_init(drv_usb_mode_t mode)
{
    /* Changing detection bits alone does not provide a host stack. */
    if (mode != DRV_USB_DEVICE || usb_ready || !drv_usb_platform_init())
        return false;
    hid_pending = false;
    hid_started = false;
    hid_needs_restart = false;
    hid_endpoint = 0;
    usb_init();
    usb_device_enter(USB_DEVICE_CAP_HID);
    usb_ready = true;
    return true;
}

void drv_usb_stop(void)
{
    if (!usb_ready) return;
    usb_disable();
    usb_ready = false;
    hid_pending = false;
    hid_started = false;
    hid_needs_restart = false;
    hid_endpoint = 0;
}

bool drv_usb_hid_try_send(const uint8_t *buf, uint32_t len)
{
    /* The supplied descriptor is a one-byte consumer-control input report.
     * EP3's vendor max_packet is not the permitted application report size. */
    if (!usb_ready || !buf || len != 1 || hid_pending || hid_needs_restart || !ude_0.configuration ||
        !udh_0.valid || udh_0.countdown || !eps[0] || !eps[4])
        return false;
    usb_ep_state_t *endpoint = eps[4];
    if (endpoint->number != 3 || !(endpoint->flags & USB_EP_IN) ||
        (endpoint->flags & (USB_EP_ABORT | USB_EP_HALTED)))
        return false;
    hid_report = buf[0]; /* Own the byte until completion; caller may reuse buf. */
    hid_endpoint = endpoint;
    hid_started = false;
    hid_pending = true;
    return true;
}

bool drv_usb_hid_busy(void) { return hid_pending; }

void drv_usb_hid_send(uint8_t *buf, uint32_t len)
{
    (void)drv_usb_hid_try_send(buf, len);
}

void drv_usb_run_loop(void)
{
    if (!usb_ready) return;
    /* Polling integration: the board port must serialize any real ISR access
     * and process queued EP0/reset/setup hooks in platform_poll(). */
    usb_isr();
    drv_usb_platform_poll();
    if (!hid_pending) return;
    if (!ude_0.configuration || !udh_0.valid || eps[4] != hid_endpoint ||
        !eps[0] || (hid_endpoint->flags & (USB_EP_ABORT | USB_EP_HALTED))) {
        /* An abort flag does not prove DMA stopped. Keep the byte unchanged
         * until a controller stop/init before accepting another report. */
        if (hid_started) hid_needs_restart = true;
        hid_pending = false;
        hid_started = false;
        return;
    }
    usb_ep_state_t *previous = usb_set_cur_ep(hid_endpoint);
    uint32_t done;
    if (!hid_started) {
        hid_endpoint->buffer = (uint32_t)(uintptr_t)&hid_report;
        hid_started = true;
        done = usb_ep_start_transfer(hid_endpoint, 1);
    } else {
        done = usb_ep_transfer(hid_endpoint);
    }
    usb_set_cur_ep(previous);
    if (done) {
        hid_pending = false;
        hid_started = false;
    }
}
