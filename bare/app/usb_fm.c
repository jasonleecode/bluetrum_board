#include "drv_fm.h"
#include "drv_usb_audio.h"

int main(void)
{
    /* ---------------- FM 初始化 ---------------- */
    drv_fm_cfg_t fm_cfg = {
        .frequency_khz = 101100,  /* 101.1MHz */
        .rf_cap = 0x10
    };
    if(!drv_fm_init(&fm_cfg)) {
        return -1;
    }

    /* ---------------- USB Audio 初始化 ---------------- */
    drv_usb_audio_init();
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
