#include "drv_usb.h"
#include <stdint.h>
#include <stdbool.h>

/* libhal.a 符号声明 */
extern void usb_init(void);
extern void usb_disable(void);
extern void usb_interrupt_disable(void);
extern void usb_isr(void);

extern void usbchk_only_device(void);
extern void usbchk_only_host(void);
extern void usbchk_switch_otg_device(void);
extern void usbchk_switch_otg_host(void);

extern void uda_init(void);
extern void uda_run_loop_execute(void);
extern void uda_set_spk_volume(uint8_t vol);
extern void uda_set_spk_mute(uint8_t en);
extern void usb_isoc_reset(void);

extern void ude_init(void);
extern void ude_run_loop_execute(void);

extern void usb_ep_init(uint8_t ep, uint8_t type, uint16_t size);
extern void usb_ep_halt(uint8_t ep);
extern void usb_ep_do_transfer(uint8_t ep, void *buf, uint32_t len);

/* 内部状态 */
static drv_usb_mode_t usb_mode;

/* ---------------- USB 初始化 ---------------- */
bool drv_usb_init(drv_usb_mode_t mode)
{
    usb_mode = mode;

    usb_disable();           /* 先关闭 USB */
    usb_init();              /* 初始化 USB 控制器 */
    usb_interrupt_disable(); /* 禁止中断，等配置好再开 */

    if (mode == DRV_USB_DEVICE) {
        usbchk_only_device();
        ude_init();
        uda_init();         /* 初始化 USB Audio */
    } else if (mode == DRV_USB_HOST) {
        usbchk_only_host();
    } else {
        return false;
    }

    /* 打开中断（HAL 内部实现） */
    usbchk_connect(); 

    return true;
}

/* ---------------- Device 模式循环 ---------------- */
void drv_usb_run_loop(void)
{
    if (usb_mode == DRV_USB_DEVICE) {
        ude_run_loop_execute();
        uda_run_loop_execute();
    }
}

/* ---------------- USB Audio 控制 ---------------- */
void drv_usb_audio_set_volume(uint8_t vol)
{
    uda_set_spk_volume(vol);
}

void drv_usb_audio_mute(bool enable)
{
    uda_set_spk_mute(enable ? 1 : 0);
}

void drv_usb_audio_send(int16_t *buf, uint32_t len)
{
    if(!buf || len == 0) return;

    /* 通过 HAL DMA / Isochronous 发送 */
    usb_isoc_reset(); /* 确保同步 */
    usb_ep_do_transfer(1, buf, len); /* EP1 假设为 Audio OUT */
}

/* ---------------- USB HID 控制 ---------------- */
void drv_usb_hid_send(uint8_t *buf, uint32_t len)
{
    if(!buf || len == 0) return;

    /* EP2 假设为 HID OUT */
    usb_ep_do_transfer(2, buf, len);
}