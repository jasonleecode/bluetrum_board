#include "usb_class_recovered.h"

/* Source recovery of all five exports in libhal.a:usb_device_hid.o.
 * Existing usb_device_run_loop.o owns udh_0 and ep3_buf; usb_comm.o owns
 * epsize_tbl. This preserves the vendor context/DMA/timer contract, including
 * its blocking behavior, and does not add a generic HID report protocol. */
extern void *memset(void *destination, int value, unsigned long size);

__attribute__((section(".usbdev.com"))) static usb_ep_state_t *hid_endpoint(void)
{
    return (usb_ep_state_t *)(uintptr_t)udh_0.endpoint;
}

__attribute__((section(".com_text.usbdev"))) void ude_tmr_isr(void)
{
    /* Two volatile reads are intentional: preserve the original ISR. */
    if (udh_0.countdown != 0)
        udh_0.countdown = (uint8_t)(udh_0.countdown - 1u);
}

__attribute__((section(".usbdev.com"))) void ude_hid_setvalid(void)
{
    udh_0.valid = 1;
    udh_0.countdown = 10;
}

__attribute__((section(".usbdev.com"))) void usbd_ep3_transmit(const void *buffer, uint16_t length)
{
    usb_ep_state_t *previous = usb_set_cur_ep(hid_endpoint());
    usb_ep_state_t *endpoint = hid_endpoint();
    endpoint->buffer = (uint32_t)(uintptr_t)buffer;
    udh_0.countdown = 30;
    usb_ep_start_transfer(endpoint, length);
    do {
        if (usb_ep_transfer(hid_endpoint()) != 0)
            break;
    } while (udh_0.countdown != 0);
    udh_0.countdown = 0;
    usb_set_cur_ep(previous);
}

__attribute__((section(".usbdev.com"))) void usb_int_ep_reset(void)
{
    usb_ep_reset(hid_endpoint());
    udh_0.valid = 0;
}

__attribute__((section(".usbdev.com"))) void udh_init(usb_hid_state_t *state)
{
    memset(ep3_buf, 0, 8);
    usb_ep_state_t *endpoint = (usb_ep_state_t *)(uintptr_t)state->endpoint;
    endpoint->registers = 0x200;
    /* Original writes buffer before flags, number, type, and packet size. */
    uint8_t flags = endpoint->flags;
    endpoint->buffer = (uint32_t)(uintptr_t)ep3_buf;
    endpoint->flags = flags | USB_EP_IN;
    endpoint->number = 3;
    endpoint->type = 2;
    endpoint->max_packet = (uint16_t)((uint16_t)epsize_tbl[3] << 6);
    usb_ep_init(endpoint);
    state->valid = 0;
}
