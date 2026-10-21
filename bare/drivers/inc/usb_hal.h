#pragma once
#include "usb_comm_recovered.h"
#include "usb_ep_recovered.h"
#include "usb_enum_recovered.h"
#include "usb_class_recovered.h"

/* High-level vendor entry binds EP0 and class contexts before use. */
void usb_device_enter(uint32_t capabilities);
void usb_device_init(uint32_t capabilities);
void usb_device_exit(void);
enum { USB_DEVICE_CAP_HID = 4 };
