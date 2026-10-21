#pragma once

#include <stddef.h>
#include <stdint.h>

/* RV32 layout recovered from libhal.a:usb_device_ep.o. Address fields are
 * deliberately 32-bit, including when this header is read by host tools. */
typedef struct {
    uint32_t registers;
    uint8_t type;
    uint8_t number;
    volatile uint8_t flags;
    uint8_t reserved;
    uint32_t buffer;
    uint32_t cursor;
    uint16_t remaining;
    uint16_t transferred;
    uint16_t max_packet;
    uint16_t padding;
} usb_ep_state_t;

_Static_assert(sizeof(usb_ep_state_t) == 24, "USB endpoint ABI size");
_Static_assert(offsetof(usb_ep_state_t, buffer) == 8, "USB buffer ABI");
_Static_assert(offsetof(usb_ep_state_t, remaining) == 16, "USB length ABI");
_Static_assert(offsetof(usb_ep_state_t, max_packet) == 20, "USB packet ABI");

enum {
    USB_EP_ABORT = 1,
    USB_EP_IN = 2,
    USB_EP_HALTED = 4,
    USB_EP_START = 8,
};

/* Index 0: selected endpoint; 1..4: IN 0..3; 5..8: OUT 0..3.
 * A valid selected endpoint is needed before halt/clear/reset, because these
 * routines restore the previous selection by dereferencing it. */
extern usb_ep_state_t *eps[9];
usb_ep_state_t *usb_set_cur_ep(usb_ep_state_t *ep);
void usb_ep_init(usb_ep_state_t *ep);
void usb_ep_halt(usb_ep_state_t *ep);
void usb_ep_clear(usb_ep_state_t *ep);
void usb_ep_reset(usb_ep_state_t *ep);
/* 0: pending/packet queued, 1: transfer complete. Counts are bytes. No copy is
 * performed: DMA owns buffer/cursor memory until the transfer completes.
 * OUT short packets complete without updating cursor/remaining. */
uint32_t usb_ep_transfer(usb_ep_state_t *ep);
uint32_t usb_ep_start_transfer(usb_ep_state_t *ep, uint16_t length);
/* Vendor polling helper: no timeout, feeds watchdog at 0x68 while pending.
 * No useful return contract is established for abort; use nonblocking APIs. */
void usb_ep_do_transfer(usb_ep_state_t *ep, uint16_t length);
