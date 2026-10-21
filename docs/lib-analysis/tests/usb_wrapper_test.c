#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "drv_usb.h"
#include "drv_usb_audio.h"
#include "drv_audio.h"
#include "usb_hal.h"

struct usb_ude_recovered ude_0;
usb_hid_state_t udh_0;
usb_ep_state_t *eps[9];
static usb_ep_state_t control, interrupt_in;
static unsigned hardware_calls, polls, starts, transfers, selections;
static uint8_t expected_report;
static bool transfer_done;
#ifndef USE_VENDOR_SHIM
static bool platform_ready;
static unsigned platform_calls;
bool drv_usb_platform_init(void) { ++platform_calls; return platform_ready; }
void drv_usb_platform_poll(void) {}
#endif

void usb_init(void) { ++hardware_calls; }
void usb_disable(void) { ++hardware_calls; }
void usb_isr(void) { ++polls; }
void usb_device_enter(uint32_t capabilities)
{
    assert(capabilities == USB_DEVICE_CAP_HID);
    ++hardware_calls;
    memset(&ude_0, 0, sizeof(ude_0));
    memset(&udh_0, 0, sizeof(udh_0));
    memset(&interrupt_in, 0, sizeof(interrupt_in));
    eps[0] = &control;
    eps[4] = &interrupt_in;
    interrupt_in.number = 3;
    interrupt_in.flags = USB_EP_IN;
}
usb_ep_state_t *usb_set_cur_ep(usb_ep_state_t *endpoint)
{
    assert(endpoint);
    ++selections;
    usb_ep_state_t *old = eps[0];
    eps[0] = endpoint;
    return old;
}
uint32_t usb_ep_start_transfer(usb_ep_state_t *endpoint, uint16_t length)
{
    ++starts;
    assert(endpoint == &interrupt_in && eps[0] == endpoint && length == 1);
    assert(*(const uint8_t *)(uintptr_t)endpoint->buffer == expected_report);
    return transfer_done;
}
uint32_t usb_ep_transfer(usb_ep_state_t *endpoint)
{
    ++transfers;
    assert(endpoint == &interrupt_in && eps[0] == endpoint);
    assert(*(const uint8_t *)(uintptr_t)endpoint->buffer == expected_report);
    return transfer_done;
}
void fmrx_dma_to_aubuf(uint8_t enabled) { (void)enabled; }

int main(void)
{
    uint8_t report = 0x40;
    assert(!drv_usb_init(DRV_USB_HOST));
    assert(!drv_usb_init((drv_usb_mode_t)123));
    assert(!drv_usb_init(DRV_USB_DEVICE));
    assert(!drv_usb_hid_try_send(&report, 1));
    drv_usb_run_loop(); drv_usb_stop();
    assert(hardware_calls == 0 && polls == 0 && starts == 0);
    assert(!drv_usb_audio_init());
    assert(!drv_usb_audio_send(NULL, 0));
    int16_t audio[2] = {0};
    assert(!drv_usb_audio_send(audio, 2));
    assert(!drv_audio_init());
    assert(!drv_audio_set_route(DRV_AUDIO_ROUTE_USB_AUDIO));
    drv_usb_audio_set_volume(50); drv_usb_audio_mute(true);
#ifdef USE_VENDOR_SHIM
    puts("PASS default USB shim refuses init; unsupported audio does not touch hardware");
#else
    assert(platform_calls == 1);
    platform_ready = true;
    assert(drv_usb_init(DRV_USB_DEVICE));
    assert(hardware_calls == 2 && platform_calls == 2);
    assert(!drv_usb_init(DRV_USB_DEVICE));
    assert(platform_calls == 2);
    assert(!drv_usb_hid_try_send(&report, 1)); /* Not enumerated. */
    ude_0.configuration = 1;
    assert(!drv_usb_hid_try_send(&report, 1));
    udh_0.valid = 1;
    udh_0.countdown = 10;
    assert(!drv_usb_hid_try_send(&report, 1));
    udh_0.countdown = 0;
    assert(!drv_usb_hid_try_send(NULL, 1));
    assert(!drv_usb_hid_try_send(&report, 0));
    assert(!drv_usb_hid_try_send(&report, 2));
    interrupt_in.flags |= USB_EP_HALTED;
    assert(!drv_usb_hid_try_send(&report, 1));
    interrupt_in.flags = USB_EP_IN;
    interrupt_in.number = 2;
    assert(!drv_usb_hid_try_send(&report, 1));
    interrupt_in.number = 3;

    assert(drv_usb_hid_try_send(&report, 1));
    expected_report = report;
    report = 0x80; /* Wrapper must own the copied byte. */
    assert(drv_usb_hid_busy() && starts == 0);
    assert(!drv_usb_hid_try_send(&report, 1));
    drv_usb_run_loop();
    assert(starts == 1 && transfers == 0 && selections == 2 && eps[0] == &control);
    assert(drv_usb_hid_busy());
    drv_usb_run_loop();
    assert(transfers == 1 && drv_usb_hid_busy());
    transfer_done = true;
    drv_usb_run_loop();
    assert(transfers == 2 && !drv_usb_hid_busy() && eps[0] == &control);

    report = 0; expected_report = 0; /* Release is an explicit second report. */
    drv_usb_hid_send(&report, 1);
    assert(drv_usb_hid_busy());
    drv_usb_run_loop();
    assert(starts == 2 && !drv_usb_hid_busy());

    transfer_done = false;
    assert(drv_usb_hid_try_send(&report, 1));
    drv_usb_run_loop();
    assert(starts == 3);
    udh_0.valid = 0; /* Reset/abort must not allow overwriting active DMA data. */
    drv_usb_run_loop();
    assert(!drv_usb_hid_busy());
    udh_0.valid = 1;
    assert(!drv_usb_hid_try_send(&report, 1));
    drv_usb_stop();
    unsigned old_polls = polls;
    drv_usb_run_loop();
    assert(polls == old_polls);
    assert(drv_usb_init(DRV_USB_DEVICE));
    ude_0.configuration = 1; udh_0.valid = 1;
    assert(drv_usb_hid_try_send(&report, 1));
    drv_usb_stop();
    assert(!drv_usb_hid_busy());
    puts("PASS USB wrapper: readiness, correct context entry, one-byte async HID ownership and restart");
#endif
    return 0;
}
