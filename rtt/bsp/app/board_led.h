/* SPDX-License-Identifier: Apache-2.0 */
#ifndef AB32_BOARD_LED_H
#define AB32_BOARD_LED_H
#include <rtthread.h>
void board_led_init(void);
void board_led_poll(rt_uint8_t key_downs);
#endif
