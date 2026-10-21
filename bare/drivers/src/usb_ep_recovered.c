/* Source recovery of libhal.a:usb_device_ep.o. Preserve vendor MMIO order,
 * polling and quirks; this is not a USB class or enumeration implementation. */
#include "usb_ep_recovered.h"

#define REG(address) (*(volatile uint32_t *)(uintptr_t)(address))
extern void delay_us(uint32_t microseconds);

usb_ep_state_t *eps[9];

__attribute__((section(".usbdev.com"))) usb_ep_state_t *usb_set_cur_ep(usb_ep_state_t *ep)
{
    usb_ep_state_t *old = eps[0];
    eps[0] = ep;
    REG(ep->registers + 0x38) = ep->number;
    return old;
}

__attribute__((section(".usbdev.com"))) static void set_buffer(uint8_t number, uint32_t address, uint8_t in)
{
    if (number >= 1 && number <= 3)
        REG(0x310u + number * 8u + (in ? 4u : 0u)) = address;
}

__attribute__((section(".usbdev.com"))) void usb_ep_init(usb_ep_state_t *ep)
{
    uint8_t in = ep->flags & USB_EP_IN;
    eps[ep->number + (in ? 1u : 5u)] = ep;
    set_buffer(ep->number, ep->buffer, in);
    ep->flags &= (uint8_t)~USB_EP_ABORT;
    ep->flags &= (uint8_t)~USB_EP_HALTED;
}

__attribute__((section(".usbdev.com"))) void usb_ep_halt(usb_ep_state_t *ep)
{
    if (ep->flags & USB_EP_HALTED)
        return;
    usb_ep_state_t *old = usb_set_cur_ep(ep);
    uint32_t base = ep->registers;
    ep->flags |= USB_EP_HALTED;
    if (!ep->number) {
        REG(base + 0x44) = 0x60;
    } else if (ep->flags & USB_EP_IN) {
        while (REG(base + 0x44) & 1u) {}
        REG(base + 0x44) = 0x10;
    } else {
        REG(base + 0x50) = 0x21;
    }
    usb_set_cur_ep(old);
}

__attribute__((section(".usbdev.com"))) void usb_ep_clear(usb_ep_state_t *ep)
{
    if (!(ep->flags & USB_EP_HALTED))
        return;
    usb_ep_state_t *old = usb_set_cur_ep(ep);
    uint32_t base = ep->registers;
    ep->flags &= (uint8_t)~USB_EP_HALTED;
    if (ep->number) {
        if (ep->flags & USB_EP_IN)
            REG(base + 0x44) = 0x40;
        else
            REG(base + 0x50) = 0x81;
    }
    usb_set_cur_ep(old);
}

__attribute__((section(".usbdev.com"))) uint32_t usb_ep_transfer(usb_ep_state_t *ep)
{
    uint32_t base = ep->registers;
    usb_set_cur_ep(ep);
    if (!(ep->flags & USB_EP_IN)) {
        if (ep->flags & USB_EP_START) {
            ep->flags &= (uint8_t)~USB_EP_START;
        } else {
            if (!(REG(base + 0x50) & 1u))
                return 0;
            uint16_t remaining = ep->remaining;
            uint16_t packet = remaining < ep->max_packet ? remaining : ep->max_packet;
            uint32_t high = REG(base + 0x5c);
            uint32_t received = (high << 8) | REG(base + 0x58);
            ep->transferred = (uint16_t)(ep->transferred + (uint16_t)received);
            if (packet != received)
                return 1;
            ep->cursor += packet;
            ep->remaining = (uint16_t)(remaining - (uint16_t)received);
            if (!ep->remaining)
                return 1;
        }
        REG(0x538) = 0x80;
        set_buffer(ep->number, ep->cursor, 0);
        REG(base + 0x50) = 0x10;
        REG(0x53c) = 0x80;
        return 0;
    }

    if (REG(base + 0x44) & 1u)
        return 0;
    uint16_t packet = ep->remaining;
    if (!(ep->flags & USB_EP_START) && !packet)
        return 1;
    ep->flags &= (uint8_t)~USB_EP_START;
    if (packet >= ep->max_packet)
        packet = ep->max_packet;
    REG(0x538) = 0x80;
    uint8_t number = ep->number;
    set_buffer(number, ep->cursor, 1);
    REG(0x308) = (1u << ((number + 16u) & 31u)) | packet;
    REG(base + 0x44) = 1;
    delay_us(2);
    REG(0x53c) = 0x80;
    ep->cursor += packet;
    ep->remaining = (uint16_t)(ep->remaining - packet);
    ep->transferred = (uint16_t)(ep->transferred + packet);
    return 0;
}

__attribute__((section(".usbdev.com"))) void usb_ep_reset(usb_ep_state_t *ep)
{
    usb_ep_state_t *old = usb_set_cur_ep(ep);
    uint32_t base = ep->registers;
    ep->flags &= (uint8_t)~USB_EP_HALTED;
    uint16_t packet = ep->max_packet;
    if (ep->flags & USB_EP_IN) {
        REG(base + 0x44) = 0x48;
        REG(base + 0x48) = ep->type == 3 ? 0x40 : ep->type == 1 ? 0x20 : 0;
        REG(base + 0x40) = packet >> 3;
    } else {
        REG(base + 0x50) = 0x90;
        REG(base + 0x54) = ep->type == 3 ? 0x40 : 0;
        REG(base + 0x4c) = packet >> 3;
    }
    usb_set_cur_ep(old);
}

__attribute__((section(".usbdev.com"))) uint32_t usb_ep_start_transfer(usb_ep_state_t *ep, uint16_t length)
{
    ep->remaining = length;
    ep->transferred = 0;
    ep->cursor = ep->buffer;
    ep->flags |= USB_EP_START;
    return usb_ep_transfer(ep);
}

__attribute__((section(".usbdev.com"))) void usb_ep_do_transfer(usb_ep_state_t *ep, uint16_t length)
{
    usb_ep_start_transfer(ep, length);
    while (!(ep->flags & USB_EP_ABORT)) {
        if (usb_ep_transfer(ep))
            break;
        REG(0x68) = 10;
    }
}
