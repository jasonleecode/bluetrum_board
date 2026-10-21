#include "drv_audio.h"
#include <stdint.h>

/* Fixed 48 kHz, two signed 16-bit samples per frame. Board must implement the
 * DAC platform hook. Low-level 1 kHz demonstration, no files or heap needed. */
static int16_t frames[48 * 2];

int main(void)
{
    for (uint32_t i = 0; i < 48; ++i) {
        int16_t sample = i < 24 ? 1000 : -1000;
        frames[2*i] = sample;
        frames[2*i+1] = sample;
    }
    if (!drv_audio_init()) return -1;
    drv_audio_set_volume(10);
    drv_audio_mute(false);
    uint32_t offset = 0;
    for (;;) {
        offset += drv_audio_write_frames(frames + offset*2, 48-offset);
        if (offset == 48) offset = 0;
        /* A full hardware FIFO returns zero; other tasks can run here. */
    }
}
