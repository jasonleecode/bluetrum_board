#pragma once

#include <stdint.h>
#include "usb_ep_recovered.h"

/* Vendor RV32 HID context. The endpoint is an address, not an endpoint number. */
typedef struct {
    uint32_t endpoint;
    volatile uint8_t valid;
    volatile uint8_t countdown;
    uint8_t reserved[2];
} usb_hid_state_t;

_Static_assert(sizeof(usb_hid_state_t) == 8, "USB HID context ABI");
extern usb_hid_state_t udh_0;
extern uint8_t ep3_buf[8];
extern const uint8_t epsize_tbl[];

void ude_tmr_isr(void);
void ude_hid_setvalid(void);
void usb_int_ep_reset(void);
void udh_init(usb_hid_state_t *state);
/* Exact vendor blocking helper: requires endpoint/timer interrupts to make
 * progress. Its countdown is measured in calls to ude_tmr_isr, not an inferred
 * time unit. Keep the buffer alive until completion or verified DMA shutdown;
 * countdown expiry does not cancel/reset DMA. No success result is returned. */
void usbd_ep3_transmit(const void *buffer, uint16_t length);
