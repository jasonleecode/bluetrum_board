#include "drv_fm.h"

int main(void);

int main(void)
{
    drv_fm_cfg_t cfg = {
        .frequency_khz = 101100, /* 101.1 MHz */
        .rf_cap = 0x10
    };

    if (!drv_fm_init(&cfg)) {
        // FM init failed, could use LED or other indicator
        return -1;
    }

    drv_fm_start();
    drv_fm_route_to_audio_buffer(true);

    while (1) {
        drv_fm_sync_dac(512);
    }

    drv_fm_stop();
    drv_fm_power_off();
}
