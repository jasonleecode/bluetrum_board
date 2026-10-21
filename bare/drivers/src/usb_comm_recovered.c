/* Reconstructed from usb_comm.o and usb_detect.o. Preserve ordered volatile
 * accesses, weak override points, section/alignment and original quirks. */
#include "usb_comm_recovered.h"
#include "usb_ep_recovered.h"
#include <stddef.h>

#define REG(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define SECTION(name) __attribute__((section(name)))
extern void *memset(void *, int, size_t);
extern void usb_dev_isr(void);

uint8_t ep0_buf[64] __attribute__((section(".buf.usb.ctrl"), aligned(4))) = {0};
const uint8_t epsize_tbl[4]
    __attribute__((section(".com_text.epsize.table"), aligned(4))) = {1, 1, 4, 16};
uint8_t usbchk_cb[2] __attribute__((aligned(4)));

void SECTION(".text") usb_disable(void)
{
    REG(0x300) = 0x20;
    REG(0x304) = 0;
    REG(0x308) = 0;
    REG(0x30c) = 0;
    REG(0x3b0) &= UINT32_C(0xffffbfff);
}

__attribute__((weak)) void SECTION(".text") usb_init(void)
{
    memset(eps, 0, 36);
    REG(0x650) |= 0x18;
    usb_disable();
    REG(0x074) &= ~UINT32_C(0x400);
    REG(0x3b0) |= 0x4000;
    __asm__ volatile ("nop\n\tnop");
    REG(0x300) = 4;
    REG(0x300) |= 0x30;
    REG(0x300) |= 1;
    REG(0x30c) = 30;
    REG(0x300) |= 2;
    usbchk_only_device();
}

void SECTION(".com_text.usb") usb_interrupt_disable(volatile uint32_t *registers)
{
    registers[11] = 0;
    registers[7] = 0;
    registers[8] = 0;
    registers[9] = 0;
    registers[10] = 0;
    (void)registers[6];
    (void)registers[2];
    (void)registers[3];
    (void)registers[4];
    (void)registers[5];
}

__attribute__((weak)) void SECTION(".com_text.irq") usb_isr(void)
{
    if (REG(0x30c) & 1u) usb_dev_isr();
}

void SECTION(".com_text.usb.dectect.sw") usbchk_switch_otg_device(void)
{
    REG(0x304) = 0x40000;
    usbchk_cb[0] = 3;
    usbchk_cb[1] = 0;
}

void SECTION(".com_text.usb.dectect.otg") usbchk_switch_otg_host(void)
{
    REG(0x304) = 0xa0000;
    usbchk_cb[0] = 2;
    usbchk_cb[1] = 0;
}

void SECTION(".com_text.usb.dectect.host") usbchk_only_host(void)
{
    REG(0x304) = 0xa0000;
    usbchk_cb[0] = 0;
    usbchk_cb[1] = 0;
}

void SECTION(".com_text.usb.dectect") usbchk_only_device(void)
{
    REG(0x304) = 0x40000;
    usbchk_cb[0] = 1;
    usbchk_cb[1] = 0;
}

__attribute__((weak)) void SECTION(".text") usbchk_switch_init_state(void)
{
    usbchk_switch_otg_device();
}

uint32_t SECTION(".com_text.usb.dectect") usbchk_connect(uint32_t mode)
{
    if (mode == 1 && usbchk_cb[0] != 1) {
        usbchk_only_device();
        return 0;
    }
    if ((REG(0x304) & 0x100u) == 0) {
        usbchk_cb[1] = 0;
        return 2;
    }
    return 0;
}
