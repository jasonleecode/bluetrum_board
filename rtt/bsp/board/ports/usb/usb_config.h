/* SPDX-License-Identifier: Apache-2.0 */
#ifndef AB32_USB_CONFIG_H
#define AB32_USB_CONFIG_H
#include <rtthread.h>
#define CONFIG_USB_PRINTF(...) rt_kprintf(__VA_ARGS__)
#define CONFIG_USB_DBG_LEVEL 0
#define CONFIG_USB_ALIGN_SIZE 4
#define USB_NOCACHE_RAM_SECTION
#define CONFIG_USB_MEMCPY_DISABLE
#define CONFIG_USBDEV_MAX_BUS 1
#define CONFIG_USBDEV_REQUEST_BUFFER_LEN 128
#define CONFIG_USBDEV_ADVANCE_DESC
/* No EP0 thread/IRQ callbacks: one RTT worker owns the entire USB stack. */
#endif
