#pragma once
#include <stdint.h>
#include "usb_ep_recovered.h"

/* Internal RV32 layout recovered from libhal.a:usb_device_enum.o.
 * This is the controller's EP0 state, not a complete USB enumeration stack.
 */
struct usb_ude_recovered {
    volatile uint32_t *regs;
    uint8_t type, number;
    uint16_t reserved6;
    uint8_t *buffer;
    const uint8_t *transmit_data;
    uint16_t remaining, reserved18;
    uint16_t max_packet, reserved22;
    uint8_t state, address, configuration, mode;
    uint32_t flags;
    uint8_t setup[8];
};
extern struct usb_ude_recovered ude_0;
extern uint8_t usb_device_qualifier[10];

void ude_init(struct usb_ude_recovered *ude);
void ude_run_loop_execute(struct usb_ude_recovered *ude);
void ude_control_flow(void);
void get_setup(struct usb_ude_recovered *ude);
uint32_t usbd_ep0_transmit(const void *buffer, uint16_t length);
void usbd_ep0_set_address(void);
void usbd_ep_reset(void);
void usbd_ep0_send_status(void);
void usbd_ep_set_stall(void);
void usbd_ep_clear_stall(void);

/* Hooks must preserve/queue state. setup points into mutable ude storage;
 * deferred consumers must copy its eight bytes before returning. */
void os_mq_ude_ep0_setup_post(const uint8_t *setup);
void os_mq_ude_reset_post(struct usb_ude_recovered *ude);
void os_mq_ude_ctl_flow_post(struct usb_ude_recovered *ude);
