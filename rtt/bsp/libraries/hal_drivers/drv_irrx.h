/* SPDX-License-Identifier: Apache-2.0 */
#ifndef AB32_DRV_IRRX_H
#define AB32_DRV_IRRX_H
#include <rtthread.h>

#define AB32_IR_QUEUE_SIZE 8u
#define AB32_IR_START      (RT_DEVICE_CTRL_BASE(Miscellaneous) + 0x20)
#define AB32_IR_STOP       (RT_DEVICE_CTRL_BASE(Miscellaneous) + 0x21)
#define AB32_IR_GET_STATUS (RT_DEVICE_CTRL_BASE(Miscellaneous) + 0x22)
enum { AB32_IR_DOWN = 1, AB32_IR_REPEAT, AB32_IR_UP };
/* ir0 is a nonblocking event stream. rt_device_read size/result are BYTES,
 * whole records only. One logical consumer; shell read/watch consume events.
 * raw: address byte, address-inverse/extended high byte, command, command-inverse
 * from least to most significant byte. Command complement is validated.
 * REPEAT means a repeated complete 32-bit frame, not a short NEC repeat burst.
 * Hardware handles short-repeat hold; it exposes no repeat-event bit.
 * UP requires hardware release plus 400 ms without a full frame; rapid
 * same-key presses inside that window are indistinguishable from repeats.
 */
struct ab32_ir_event {
    rt_uint32_t raw, tick;
    rt_uint8_t kind, reserved[3];
};
struct ab32_ir_status {
    rt_uint32_t raw, frames, invalid, releases, dropped;
    rt_uint8_t running, pressed, queued, reserved;
};
/* START clears old events/stats on an off->on transition; repeated START is
 * harmless. STOP retains queued events and emits UP for a held key. Closing
 * the device does not implicitly stop it. All public device calls are thread-only.
 * Overflow drops the oldest event; use status to resynchronize after loss.
 */
/* Call from the existing main loop every 10 ms, even without a queue reader. */
void ab32_irrx_poll(void);
#endif
