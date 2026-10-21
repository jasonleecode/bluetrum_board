/* SPDX-License-Identifier: Apache-2.0 */
#ifndef AB32_DRV_FMRX_H
#define AB32_DRV_FMRX_H
#include <rtthread.h>

/* rt_device_control(rt_device_find("fm0"), command, argument).
 * START/TUNE: rt_uint32_t kHz, VOLUME: rt_uint32_t 0..100,
 * STATUS: struct ab32_fmrx_status; STOP: RT_NULL.
 * START while playing is rejected; TUNE requires an active receiver.
 */
enum {
    AB32_FMRX_START = 0x1000, AB32_FMRX_TUNE, AB32_FMRX_STOP,
    AB32_FMRX_VOLUME, AB32_FMRX_STATUS
};
struct ab32_fmrx_status {
    rt_uint32_t frequency_khz;
    rt_uint32_t volume;
    rt_bool_t running;
    rt_err_t last_error;
};

/* Private sound0 ownership interface. FM must stop RF/DMA before release. */
rt_err_t ab32_sound_fm_acquire(rt_uint32_t *buffer_words);
void ab32_sound_fm_release(void);
void ab32_sound_fm_volume(rt_uint8_t volume);
#endif
