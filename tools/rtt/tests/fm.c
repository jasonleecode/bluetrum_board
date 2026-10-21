/* Actual RT-Thread FM/sound callbacks; MMIO, RTOS and RF are modeled. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "board.h"
volatile uint32_t extra_regs[32];
static rt_tick_t tick;
static int locks, clock_on, power_on_calls, power_off_calls, sync_calls;
static int route, force_timeout, reset_calls, pcm_calls;
static uint32_t tuned;
static int i2s_on, i2s_calls;
int ab32_fmrx_wait_pll(volatile uint32_t *status);
void rt_assert_handler(const char *e, const char *f, rt_size_t l)
{ fprintf(stderr, "%s:%lu %s\n", f, (unsigned long)l, e); abort(); }
rt_tick_t rt_tick_from_millisecond(rt_int32_t ms) { return ms; }
rt_tick_t rt_tick_get(void) { return tick++; }
rt_err_t rt_thread_mdelay(rt_int32_t ms) { tick += ms; return RT_EOK; }
void hal_mdelay(uint32_t ms) { tick += ms; }
rt_err_t rt_mutex_take(rt_mutex_t m, rt_int32_t t)
{ (void)m; (void)t; locks++; return RT_EOK; }
rt_err_t rt_mutex_release(rt_mutex_t m)
{ (void)m; assert(locks > 0); locks--; return RT_EOK; }
rt_err_t rt_sem_release(rt_sem_t s) { (void)s; return RT_EOK; }
rt_err_t rt_sem_take(rt_sem_t s, rt_int32_t t) { (void)s; (void)t; return RT_EOK; }
void *rt_memset(void *s, int c, rt_ubase_t n) { return memset(s, c, n); }
int rt_kprintf(const char *f, ...) { (void)f; return 0; }
void hal_rcu_periph_clk_enable(uint32_t c) { if (c == RCU_IIS) return; assert(c == RCU_FMRX); clock_on = 1; }
void hal_rcu_periph_clk_disable(uint32_t c) { if (c == RCU_IIS) return; assert(c == RCU_FMRX); clock_on = 0; }
int i2s_src_init(uint32_t mapping) { assert(mapping == 1 && !clock_on); i2s_on = 1; i2s_calls++; return 0; }
void i2s_src_exit(uint32_t mapping) { assert(mapping == 1); i2s_on = 0; }
void fmrx_set_pll(uint32_t mode, uint32_t freq) {
    volatile uint32_t status = force_timeout ? 0 : 0x80;
    assert(mode == 1 && clock_on);
    if (!ab32_fmrx_wait_pll(&status)) tuned = freq;
}
void fmrx_power_on(uint32_t v) { assert(v == 0); power_on_calls++; fmrx_set_pll(1, 8750); }
void fmrx_dma_to_aubuf(uint8_t en) { assert(en == 1 && clock_on); route = en; }
void fmrx_dac_sync(uint32_t words) { assert(words == 256 && route); sync_calls++; }
void fmrx_dac_sync_reset(void) { assert(!route); reset_calls++; }
void fmrx_power_off(void) { assert(clock_on); route = 0; power_off_calls++; }
#include "../../../rtt/bsp/board/ports/audio/drv_sound.c"
#include "../../../rtt/bsp/board/ports/audio/drv_fmrx.c"
static rt_err_t pcm_open_stub(rt_device_t d, rt_uint16_t f)
{ (void)d; (void)f; pcm_calls++; return RT_EOK; }
static rt_err_t pcm_close_stub(rt_device_t d)
{ (void)d; assert(locks == 0); pcm_calls++; return RT_EOK; }
static rt_ssize_t pcm_write_stub(rt_device_t d, rt_off_t p, const void *b, rt_size_t s)
{ (void)d; (void)p; (void)b; pcm_calls++; return s; }
static rt_err_t pcm_control_stub(rt_device_t d, int c, void *a)
{ (void)d; (void)c; (void)a; pcm_calls++; return RT_EOK; }
int main(void) {
    uint32_t fifo[256], v = 101100, words;
    struct ab32_fmrx_status state;
    snd_dev.rx_fifo = (uint8_t *)fifo;
    snd_dev.audio.parent.user_data = &snd_dev;
    snd_dev.replay_config.samplerate = 44100;
    snd_dev.replay_config.channels = 1;
    snd_dev.replay_config.samplebits = 16;
    snd_dev.volume = 42;
    struct rt_audio_caps caps = {.main_type = AUDIO_TYPE_OUTPUT, .sub_type = AUDIO_DSP_CHANNELS};
    caps.udata.config.channels = 1;
    assert(sound_configure(&snd_dev.audio, &caps) == -RT_EINVAL); /* I2S only stereo */
    caps.udata.config.channels = 2;
    assert(sound_configure(&snd_dev.audio, &caps) == RT_EOK);
    pcm_ops.open = pcm_open_stub; pcm_ops.close = pcm_close_stub;
    pcm_ops.write = pcm_write_stub; pcm_ops.control = pcm_control_stub;
    assert(fm_control(NULL, AB32_FMRX_TUNE, &v) == -RT_EIO);
    assert(fm_control(NULL, AB32_FMRX_START, NULL) == -RT_EINVAL);
    v = 101101;
    assert(fm_control(NULL, AB32_FMRX_START, &v) == -RT_EINVAL && !power_on_calls);
    v = 101100;
    assert(sound_device_open(&snd_dev.audio.parent, RT_DEVICE_OFLAG_WRONLY) == RT_EOK);
    assert(fm_control(NULL, AB32_FMRX_START, &v) == -RT_EBUSY);
    assert(sound_device_close(&snd_dev.audio.parent) == RT_EOK);
    assert(fm_control(NULL, AB32_FMRX_START, &v) == RT_EOK);
    assert(fm_status.running && snd_dev.running && snd_dev.dma_to_aubuf);
    assert(tuned == 10110 && route && clock_on && (DACDIGCON0 & 0x3c) == 8);
    assert(ab32_sound_fm_acquire(&words) == -RT_EBUSY);
    assert(fm_control(NULL, AB32_FMRX_START, &v) == -RT_EBUSY);
    int before = pcm_calls;
    assert(sound_device_init(&snd_dev.audio.parent) == -RT_EBUSY);
    assert(sound_device_open(&snd_dev.audio.parent, RT_DEVICE_OFLAG_WRONLY) == -RT_EBUSY);
    assert(sound_device_write(&snd_dev.audio.parent, 0, fifo, sizeof fifo) == -RT_EBUSY);
    assert(sound_device_control(&snd_dev.audio.parent, AUDIO_CTL_START, &v) == -RT_EBUSY);
    assert(pcm_calls == before);
    assert(sound_start(&snd_dev.audio, AUDIO_STREAM_REPLAY) == -RT_EBUSY);
    assert(sound_stop(&snd_dev.audio, AUDIO_STREAM_REPLAY) == -RT_EBUSY);
    assert(sound_transmit(&snd_dev.audio, fifo, NULL, sizeof fifo) == 0);
    fm_sync_once(); assert(sync_calls == 1);
    v = 108000; assert(fm_control(NULL, AB32_FMRX_TUNE, &v) == RT_EOK && tuned == 10800);
    v = 101; assert(fm_control(NULL, AB32_FMRX_VOLUME, &v) == -RT_EINVAL);
    v = 25; assert(fm_control(NULL, AB32_FMRX_VOLUME, &v) == RT_EOK && (DACVOLCON & 0xffff) == 25*327);
    force_timeout = 1; tick = (rt_tick_t)-50; v = 87500;
    assert(fm_control(NULL, AB32_FMRX_TUNE, &v) == -RT_ETIMEOUT);
    assert(!fm_status.running && !snd_dev.running && !snd_dev.dma_to_aubuf && !route && !clock_on);
    assert(reset_calls == 1 && power_off_calls == 1);
    assert(snd_dev.replay_config.samplerate == 44100 && (DACDIGCON0 & 0x3c) == 4);
    assert((DACVOLCON & 0xffff) == 42*327);
    assert(fm_control(NULL, AB32_FMRX_STATUS, &state) == RT_EOK && state.last_error == -RT_ETIMEOUT);
    fm_sync_once(); assert(sync_calls == 1);
    assert(fm_control(NULL, AB32_FMRX_STOP, NULL) == RT_EOK && power_off_calls == 1);
    assert(fm_control(NULL, AB32_FMRX_START, &v) == -RT_ETIMEOUT && !snd_dev.dma_to_aubuf);
    force_timeout = 0;
    assert(fm_control(NULL, AB32_FMRX_START, &v) == RT_EOK);
    assert(fm_control(NULL, AB32_FMRX_STOP, NULL) == RT_EOK);
    assert(sound_device_open(&snd_dev.audio.parent, RT_DEVICE_OFLAG_WRONLY) == RT_EOK);
    assert(sound_device_write(&snd_dev.audio.parent, 0, fifo, sizeof fifo) == sizeof fifo);
    assert(sound_device_close(&snd_dev.audio.parent) == RT_EOK);
    assert(!locks);
    assert(i2s_calls == 0); /* FM's 38 kHz path must not enable I2S */
    assert(sound_start(&snd_dev.audio, AUDIO_STREAM_REPLAY) == RT_EOK && i2s_on);
    assert(sound_stop(&snd_dev.audio, AUDIO_STREAM_REPLAY) == RT_EOK && !i2s_on);
    assert(!locks && i2s_calls == 1);
    puts("PASS: FM tune/volume/sync, DAC/PCM ownership, timeout/unwind/restart, I2S stereo/gating");
}
