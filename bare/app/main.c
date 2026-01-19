#include "drv_fm.h"
#include <stdio.h>

int main(void)
{
    drv_fm_cfg_t cfg = {
        .frequency_khz = 101100, /* 101.1 MHz */
        .rf_cap = 0x10
    };

    if (!drv_fm_init(&cfg)) {
        printf("FM init failed\n");
        return -1;
    }

    drv_fm_start();

    int16_t audio_buf[512];
    while (1) {
        uint32_t samples = drv_fm_get_audio(audio_buf, 512);
        if (samples > 0) {
            /* 这里可以送到 DAC / USB Audio */
        }
    }

    drv_fm_stop();
    drv_fm_power_off();
}