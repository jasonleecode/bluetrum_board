#include "drv_adc.h"
#include "drv_fm.h"
#include "drv_usb_audio.h"
#include <stdio.h>

#define AUDIO_BUF_SIZE 512

int16_t fm_buf[AUDIO_BUF_SIZE];
int16_t adc_buf[AUDIO_BUF_SIZE];

int main(void)
{
    /* ---------------- FM 初始化 ---------------- */
    drv_fm_cfg_t fm_cfg = {
        .frequency_khz = 101100,  /* 101.1MHz */
        .rf_cap = 0x10
    };
    if(!drv_fm_init(&fm_cfg)) {
        printf("FM init failed\n");
        return -1;
    }

    /* ---------------- ADC 初始化 ---------------- */
    drv_adc_cfg_t adc_cfg = {
        .channel = DRV_ADC_CH_MIC,
        .gain = 8,
        .sample_rate = 16000
    };
    drv_adc_init(&adc_cfg);
    drv_adc_start();

    /* ---------------- USB Audio 初始化 ---------------- */
    drv_usb_audio_init();
    drv_usb_audio_set_volume(50);

    /* ---------------- 启动 FM ---------------- */
    drv_fm_start();

    while(1)
    {
        /* 1️⃣ 读取 FM 音频 */
        uint32_t fm_samples = drv_fm_get_audio(fm_buf, AUDIO_BUF_SIZE);

        /* 2️⃣ 读取 ADC 音频 */
        uint32_t adc_samples = drv_adc_read(adc_buf, AUDIO_BUF_SIZE);

        /* 3️⃣ 将 FM 音频送 USB 输出 */
        if(fm_samples > 0) {
            drv_usb_audio_send(fm_buf, fm_samples);
        }

        /* 4️⃣ 将 ADC 音频送 USB 输出（可选混合） */
        if(adc_samples > 0) {
            drv_usb_audio_send(adc_buf, adc_samples);
        }
    }

    drv_fm_stop();
    drv_fm_power_off();
    return 0;
}