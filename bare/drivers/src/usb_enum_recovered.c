/* Behavioral reconstruction of libhal.a:usb_device_enum.o.
 * Preserves the vendor EP0 state machine, including its unsupported-request
 * and zero-length-transfer behavior. See USB_RECOVERY.md for limitations. */
#include "usb_enum_recovered.h"
#include <stddef.h>

_Static_assert(sizeof(void *) == 4, "vendor USB ABI requires RV32");
_Static_assert(sizeof(struct usb_ude_recovered) == 40, "ude ABI size");
_Static_assert(offsetof(struct usb_ude_recovered, setup) == 32, "setup ABI offset");

extern uint8_t ep0_buf[];
extern void usb_interrupt_disable(volatile uint32_t *regs);
extern void usb_int_ep_reset(void);
extern void delay_us(uint32_t us);
extern void *memcpy(void *, const void *, size_t);

uint8_t usb_device_qualifier[10] __attribute__((aligned(4)));
#define REG(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define FLAGS_BYTE(u) (((uint8_t *)&(u)->flags)[3])

__attribute__((section(".usbdev.com"))) static void usb_interrupt_init(struct usb_ude_recovered *u)
{
    volatile uint32_t *regs = u->regs;
    usb_interrupt_disable(regs);
    regs[44 / 4] = 4;
    regs[28 / 4] = 1;
    if (u->mode & 2) regs[36 / 4] = 4;
}

__attribute__((section(".usbdev.com"))) static uint32_t do_transferring(void)
{
    uint16_t length = ude_0.remaining;
    uint32_t final = 1;
    if (length) {
        if (length >= ude_0.max_packet) {
            length = ude_0.max_packet;
            final = 0;
        }
        memcpy(ude_0.buffer, ude_0.transmit_data, length);
        ude_0.transmit_data += length;
        ude_0.remaining -= length;
        REG(0x308) = 0x10000u | length;
    }
    ude_0.regs[68 / 4] = final ? 10 : 2;
    ude_0.state = final ? 0 : 1;
    return 1;
}

__attribute__((section(".usbdev.com"))) uint32_t usbd_ep0_transmit(const void *buffer, uint16_t length)
{
    ude_0.transmit_data = buffer;
    ude_0.remaining = length;
    return do_transferring();
}

__attribute__((section(".usbdev.com"))) void get_setup(struct usb_ude_recovered *u)
{
    memcpy(u->setup, u->buffer, 8);
    volatile uint32_t *regs = u->regs;
    uint32_t count = regs[88 / 4];
    while (count--) (void)regs[128 / 4];
}

__attribute__((section(".usbdev.com"))) void usbd_ep0_set_address(void)
{
    ude_0.max_packet = 64;
    if (!ude_0.configuration) {
        ude_0.address = ude_0.setup[2];
        FLAGS_BYTE(&ude_0) |= 2;
    }
}

__attribute__((section(".usbdev.com"))) __attribute__((weak)) void usbd_ep_reset(void)
{
    REG(0x300) &= ~6u;
    REG(0x300) |= 4u;
    delay_us(5);
    ude_0.address = 0;
    ude_0.configuration = 0;
    ude_0.state = 0;
    usb_interrupt_init(&ude_0);
    if (ude_0.mode & 4) usb_int_ep_reset();
    REG(0x300) |= 2u;
}

__attribute__((section(".usbdev.com"))) __attribute__((weak)) void ude_control_flow(void)
{
    usb_ep_state_t *previous = usb_set_cur_ep((usb_ep_state_t *)&ude_0);
    volatile uint32_t *regs = ude_0.regs;
    if (ude_0.flags & 0x02000000u) {
        FLAGS_BYTE(&ude_0) &= (uint8_t)~2u;
        regs[0] = ude_0.address | 0x80u;
    }
    uint32_t csr = regs[68 / 4];
    if (csr & 4) {
        regs[68 / 4] = 0;
        ude_0.state = 0;
    }
    if (csr & 16) {
        regs[68 / 4] = 128;
        ude_0.state = 0;
    }
    switch (ude_0.state) {
    case 1:
        if (!(csr & 2)) do_transferring();
        break;
    case 2:
        if (csr & 1) {
            ude_0.state = 0;
            regs[68 / 4] = 72;
        }
        break;
    case 0:
        if (csr & 1) {
            get_setup(&ude_0);
            uint8_t request_type = ude_0.setup[0];
            if (request_type & 0x80) regs[68 / 4] = 64;
            if (((request_type >> 5) & 3) <= 1)
                os_mq_ude_ep0_setup_post(ude_0.setup);
        }
        break;
    default:
        break;
    }
    usb_set_cur_ep(previous);
}

__attribute__((section(".usbdev.com"))) void usbd_ep0_send_status(void) { ude_0.regs[68 / 4] = 72; }
__attribute__((section(".usbdev.com"))) void usbd_ep_set_stall(void) { ude_0.regs[68 / 4] |= 32; }
__attribute__((section(".usbdev.com"))) void usbd_ep_clear_stall(void) { ude_0.regs[68 / 4] &= ~4u; }

__attribute__((section(".usbdev.com"))) void ude_run_loop_execute(struct usb_ude_recovered *u)
{
    volatile uint32_t *regs = u->regs;
    uint32_t bus = regs[24 / 4] & regs[44 / 4];
    uint32_t tx = regs[8 / 4] & regs[28 / 4];
    (void)regs[16 / 4];
    (void)regs[36 / 4];
    if (bus & 4) {
        FLAGS_BYTE(u) |= 1;
        os_mq_ude_reset_post(u);
        return;
    }
    if (bus & 1) FLAGS_BYTE(u) |= 1;
    else if (bus & 2) regs[44 / 4] = 4;
    if (tx & 1) os_mq_ude_ctl_flow_post(u);
}

__attribute__((section(".usbdev.com"))) void ude_init(struct usb_ude_recovered *u)
{
    u->regs = (volatile uint32_t *)(uintptr_t)0x200;
    u->buffer = ep0_buf;
    u->type = 0;
    u->number = 0;
    u->max_packet = 8;
    REG(0x314) = (uintptr_t)ep0_buf;
    eps[0] = (usb_ep_state_t *)u;
    eps[5] = (usb_ep_state_t *)u;
    u->address = 0;
    u->configuration = 0;
    u->state = 0;
    eps[1] = (usb_ep_state_t *)u;
    usb_interrupt_init(u);
}
