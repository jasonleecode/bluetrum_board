/* SPDX-License-Identifier: Apache-2.0 */
#ifndef AB32_BOARD_KEYS_H
#define AB32_BOARD_KEYS_H
#include <rtthread.h>
#define AB32_KEYS_COUNT 3u
#define AB32_KEYS_QUEUE 8u
#define AB32_KEYS_GET_STATUS (RT_DEVICE_CTRL_BASE(Miscellaneous) + 0x30)
enum { AB32_KEY_DOWN = 1, AB32_KEY_UP, AB32_KEY_LONG };
struct ab32_key_event {
    rt_uint32_t tick, held_ms;
    rt_uint8_t key, kind, reserved[2]; /* key 1=S2/PF1, 2=S3/PF0, 3=S4/PA2 */
};
struct ab32_keys_status {
    rt_uint32_t dropped, read_errors;
    rt_uint8_t raw_mask, pressed_mask, queued, reserved;
};
/* Called by main every 10 ms. keys0 read sizes/results are bytes, whole records
 * only; one logical consumer. Queue overflow drops the oldest event. */
void board_keys_poll(void);
#endif
