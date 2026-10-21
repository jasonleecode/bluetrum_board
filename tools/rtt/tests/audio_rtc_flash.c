#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "board.h"
volatile uint32_t extra_regs[32];
static int page_calls, read_calls, erase_calls;
static uint32_t last_address;
void rt_assert_handler(const char *ex, const char *func, rt_size_t line) {
    fprintf(stderr, "%s:%lu %s\n", func, (unsigned long)line, ex); abort();
}
rt_tick_t rt_tick_from_millisecond(rt_int32_t ms) { return ms; }
void hal_mdelay(uint32_t ms) { (void)ms; }
rt_err_t rt_sem_release(rt_sem_t sem) { (void)sem; return RT_EOK; }
rt_err_t rt_sem_take(rt_sem_t sem, rt_int32_t timeout) { (void)sem; (void)timeout; return RT_EOK; }
int rt_kprintf(const char *fmt, ...) { (void)fmt; return 0; }
uint16_t os_spiflash_read(void *buf, uint32_t addr, uint16_t len) {
    (void)buf; assert(len <= 256); last_address = addr; read_calls++; return 0xabcd;
}
void os_spiflash_program(const void *buf, uint32_t addr, uint16_t len) {
    (void)buf; assert(len == 256 && addr % 256 == 0); last_address = addr; page_calls++;
}
void os_spiflash_erase(uint32_t addr) { last_address = addr; erase_calls++; }
#include "../../../rtt/bsp/board/ports/audio/drv_sound.c"
#undef DBG_TAG
#undef DBG_LVL
#include "../../../rtt/bsp/libraries/hal_drivers/drv_rtc.c"
#undef LOG_TAG
#undef DBG_TAG
#include "../../../rtt/bsp/libraries/hal_drivers/drv_flash.c"

int main(void) {
    struct rt_audio_device audio = {0};
    struct rt_audio_caps caps = {0};
    audio.parent.user_data = &snd_dev;
    snd_dev.replay_config.samplerate = 48000;
    snd_dev.replay_config.channels = 2;
    snd_dev.replay_config.samplebits = 16;
    caps.main_type = AUDIO_TYPE_OUTPUT;
    caps.sub_type = AUDIO_DSP_SAMPLEBITS;
    caps.udata.config.samplebits = 24;
    assert(sound_configure(&audio, &caps) == -RT_EINVAL);
    assert(snd_dev.replay_config.samplebits == 16);
    caps.main_type = 0xffff;
    assert(sound_configure(&audio, &caps) == -RT_ENOSYS);
    assert(sound_getcaps(&audio, &caps) != RT_EOK);
    assert(sound_start(&audio, AUDIO_STREAM_RECORD) == -RT_ENOSYS);
    DACDIGCON0 = BIT(0) | BIT(10); AUBUFCON = BIT(4); snd_dev.running = RT_TRUE;
    assert(sound_stop(&audio, AUDIO_STREAM_REPLAY) == RT_EOK);
    assert(!snd_dev.running && !(DACDIGCON0 & (BIT(0) | BIT(10))) && !(AUBUFCON & BIT(4)));
    sound_init(&audio);
    assert(PLL1DIV == (uint32_t)((245.76 * 65536) / 26)); /* correct 48k PLL */

    time_t stamp = (time_t)-1;
    assert(ab32_rtc_set_secs(&stamp) == -RT_EINVAL);
    stamp = (time_t)0x100000000ULL;
    assert(ab32_rtc_set_secs(&stamp) == -RT_EINVAL);
    stamp = (time_t)-1;
    assert(ab32_rtc_get_secs(&stamp) == RT_EOK && stamp == 0);
    assert(ab32_rtc_ops.get_alarm == NULL && ab32_rtc_ops.set_alarm == NULL);
    _init_rtc_clock();
    assert(RTCDAT == RTC_CON2_32K_SELECT);

    uint8_t buffer[768] = {0};
    assert(fal_flash_read(0, buffer, sizeof(buffer)) == sizeof(buffer));
    assert(read_calls == 3 && last_address == 512); /* CRC is not byte count */
    assert(fal_flash_write(0, buffer, sizeof(buffer)) == sizeof(buffer));
    assert(page_calls == 3 && last_address == 512);
    assert(fal_flash_write(1, buffer, 256) == -RT_EINVAL);
    assert(fal_flash_read(1024*1024-1, buffer, 2) == -RT_EINVAL);
    assert(fal_flash_erase(0, 8192) == 8192 && erase_calls == 2 && last_address == 4096);
    assert(fal_flash_erase(1, 4096) == -RT_EINVAL);
    puts("PASS: audio format/stop/PLL, RTC time_t/range/clock, Flash bounds/page splitting/returns");
}
