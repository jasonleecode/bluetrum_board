/* SPDX-License-Identifier: Apache-2.0 */
#ifndef AB32_USB_PORT_H
#define AB32_USB_PORT_H
#include <stdint.h>
#include <stdbool.h>
struct ab32_usb_stats { uint32_t resets, setups, in_packets, out_packets, errors; };
extern struct ab32_usb_stats ab32_usb_stats;
void ab32_usb_poll(void);
bool ab32_usb_ep_ready(uint8_t ep);
#endif
