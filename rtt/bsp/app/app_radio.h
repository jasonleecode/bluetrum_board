/* SPDX-License-Identifier: Apache-2.0 */
#ifndef AB32_APP_RADIO_H
#define AB32_APP_RADIO_H
#include <rtthread.h>
void app_radio_init(void);
void app_radio_poll(rt_uint8_t key_downs);
#endif
