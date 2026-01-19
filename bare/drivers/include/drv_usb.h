#pragma once
#include <stdint.h>
#include <stdbool.h>

/* USB 类型 */
typedef enum {
    DRV_USB_DEVICE = 0,
    DRV_USB_HOST
} drv_usb_mode_t;

/* USB 初始化 */
bool drv_usb_init(drv_usb_mode_t mode);

/* USB Audio 控制 */
void drv_usb_audio_set_volume(uint8_t vol);
void drv_usb_audio_mute(bool enable);
void drv_usb_audio_send(int16_t *buf, uint32_t len);

/* USB HID 控制 */
void drv_usb_hid_send(uint8_t *buf, uint32_t len);

/* USB 任务循环（Device 模式下必须调用） */
void drv_usb_run_loop(void);