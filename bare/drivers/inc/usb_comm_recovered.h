#pragma once
#include <stdint.h>

/* libhal.a:usb_comm.o + usb_detect.o ABI. These are hardware primitives,
 * not a complete device stack or a host controller implementation. */
void usb_disable(void);
void usb_init(void);
void usb_interrupt_disable(volatile uint32_t *registers);
void usb_isr(void);
void usbchk_only_device(void);
void usbchk_only_host(void);
void usbchk_switch_otg_device(void);
void usbchk_switch_otg_host(void);
void usbchk_switch_init_state(void);
uint32_t usbchk_connect(uint32_t mode);
extern uint8_t ep0_buf[64];
extern const uint8_t epsize_tbl[4];
extern uint8_t usbchk_cb[2];
