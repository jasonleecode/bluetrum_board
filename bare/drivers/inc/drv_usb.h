#pragma once
#include <stdint.h>
#include <stdbool.h>

/* USB 类型 */
typedef enum {
    DRV_USB_DEVICE = 0,
    DRV_USB_HOST
} drv_usb_mode_t;

/* Board port contract: real USB clocks/pins, DMA memory/sections, delay/timer,
 * serialized IRQ/polling, and standard/HID SETUP handling through os_mq_ude_*
 * hooks. The supplied weak init returns false until that port exists.
 */
bool drv_usb_platform_init(void);
void drv_usb_platform_poll(void);

/* Consumer-control HID Device only; Host and invalid modes fail before
 * hardware access. true means started, not enumerated by the host. */
bool drv_usb_init(drv_usb_mode_t mode);
void drv_usb_stop(void);

/* USB Audio 控制 */
void drv_usb_audio_set_volume(uint8_t vol);
void drv_usb_audio_mute(bool enable);
bool drv_usb_audio_enable_fm_dma(bool enable);
bool drv_usb_audio_send(int16_t *buf, uint32_t len);

/* One-byte consumer-control input report on EP3 IN. Copies the byte and
 * queues one asynchronous transfer; false means not configured/ready, busy,
 * invalid size, or unavailable endpoint. Poll until busy is false; reset or
 * abort can also clear busy, so it is not a host delivery acknowledgement.
 * Interrupted active transfers require stop/init before another report.
 * Send a zero report separately to release keys after a completed press. */
bool drv_usb_hid_try_send(const uint8_t *buf, uint32_t len);
bool drv_usb_hid_busy(void);
/* Compatibility wrapper; use try_send when rejection must be observable. */
void drv_usb_hid_send(uint8_t *buf, uint32_t len);

/* USB 任务循环（Device 模式下必须调用） */
void drv_usb_run_loop(void);
