/* SPDX-License-Identifier: Apache-2.0 */
#ifndef AB32_DRV_AUDEC_H
#define AB32_DRV_AUDEC_H
#include <rtthread.h>
#include <rtdevice.h>

#define AB32_AUDEC_BEGIN      (RT_DEVICE_CTRL_BASE(Miscellaneous) + 0x50)
#define AB32_AUDEC_END        (RT_DEVICE_CTRL_BASE(Miscellaneous) + 0x51)
#define AB32_AUDEC_RESET      (RT_DEVICE_CTRL_BASE(Miscellaneous) + 0x52)
#define AB32_AUDEC_DECODE     (RT_DEVICE_CTRL_BASE(Miscellaneous) + 0x53)
#define AB32_AUDEC_GET_STATUS (RT_DEVICE_CTRL_BASE(Miscellaneous) + 0x54)
#define AB32_AUDEC_WORK_BASE  0x54000u
#define AB32_AUDEC_WORK_SIZE  0x3000u
#define AB32_AUDEC_SAMPLES    576u
#define AB32_AUDEC_BANDS      39u
#define AB32_AUDEC_PCM_BYTES (AB32_AUDEC_SAMPLES * 4u)

/* audec0 is a synchronous, thread-only hardware back end. Supported profile:
 * MPEG-1 Layer III, 44100 Hz, mono input -> interleaved signed 16-bit L/R PCM.
 * This is NOT an MP3 byte-stream API. spectrum/bands use the recovered AUDEC
 * quantized-spectrum/band-descriptor format, AFTER software Huffman parsing.
 * One request is one granule. block_type: 0=normal, 1=start, 2=short, 3=stop.
 * Caller supplies 576 spectrum and 39 band words, aligned to four bytes.
 * pcm is four-byte aligned, pcm_bytes >= 2304; bytes after 2304 are untouched.
 * Inputs/output must not overlap the hardware work area or driver DMA buffer.
 * timeout_ms: 0 selects 200 ms; otherwise 1..1000 ms. A timeout/guard error
 * resets and gates the engine, invalidates history and requires RESET or END.
 * Output is copied from private DMA storage ONLY on success, so error returns
 * leave the caller's output unchanged and no DMA targets caller-owned memory.
 */
struct ab32_audec_granule {
    const rt_uint32_t *spectrum;
    const rt_uint32_t *bands;
    void *pcm;
    rt_size_t pcm_bytes;
    rt_uint32_t timeout_ms;
    rt_uint8_t block_type;
};

struct ab32_audec_status {
    rt_uint32_t work_base, work_bytes;
    rt_uint32_t granules, timeouts, errors;
    rt_uint32_t last_status, last_control, last_ticks;
    rt_err_t last_error;
    rt_uint8_t active, faulted;
};

/* BEGIN/RESET/END take NULL. BEGIN claims the entire stream for the calling
 * thread; another thread cannot reset, decode or end it. Call END before the
 * owner exits. RESET discards filter/overlap history; successive DECODE calls
 * preserve it. END with no owner is harmless. All commands reject ISR,
 * scheduler-locked or IRQ-disabled contexts. Contended calls return -RT_EBUSY.
 * No automatic start, playback, worker thread or dynamic allocation.
 */
rt_bool_t ab32_audec_power_busy(void); /* PM observer; scheduler must be locked */
#endif
