#include "drv_fm.h"
#include "drv_usb_audio.h"

/* Control-flow skeleton only: DAC/audio-buffer setup and the FM-to-USB data
 * path are not yet validated. Replace 512 with the verified buffer count.
 * See docs/lib-analysis/FM_RECOVERY.md.
 */
int main(void)
{
    /* The current port is HID-only; do not power FM on if audio is absent. */
    if (!drv_usb_audio_init()) return -1;
    /* ---------------- FM 初始化 ---------------- */
    drv_fm_cfg_t fm_cfg = {
        .frequency_khz = 101100,  /* 101.1MHz */
        .rf_cap = 0x10
    };
    if(!drv_fm_init(&fm_cfg)) {
        return -1;
    }

    /* ---------------- USB Audio 初始化 ---------------- */
    drv_usb_audio_set_volume(50);

    /* ---------------- 启动 FM ---------------- */
    drv_fm_start();
    drv_usb_audio_enable_fm_dma(true);

    while(1)
    {
        drv_fm_sync_dac(512);
    }

    drv_fm_stop();
    drv_fm_power_off();
    return 0;
}
