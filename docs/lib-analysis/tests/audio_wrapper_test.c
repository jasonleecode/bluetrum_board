#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "drv_adc.h"
#include "drv_audio.h"
#include "dac_output.h"

static unsigned adc_calls, starts, stops, notices, irqs;
static bool prepared, mapping, adc_failure;
static uint16_t configured_units;
static uint32_t dma_address = 0x12000;
static uint8_t channel;
static unsigned releases;
#ifndef DEFAULT_PLATFORM
bool drv_adc_platform_init(const drv_adc_cfg_t *cfg) { assert(cfg->channel==DRV_ADC_CH_MIC); return prepared; }
bool drv_adc_platform_dma(void *buf, size_t bytes, uint16_t units, uint32_t *address)
{
    assert(buf && units==configured_units);
    if (!mapping || bytes < (size_t)units*4u) return false; /* test port's explicit model */
    *address=dma_address;
    return true;
}
void drv_adc_platform_stop(void) { ++releases; }
#endif
int sdadc_set_channel(uint8_t c) { ++adc_calls; channel=c; return 0; }
int sdadc_set_sample_rate(uint32_t hz) { ++adc_calls; assert(hz==16000); return adc_failure?-1:0; }
int sdadc_set_gain(uint16_t gain) { ++adc_calls; assert(gain==0x168); return 0; }
int sdadc_set_dma_samples(uint16_t n) { ++adc_calls; configured_units=n; return 0; }
int sdadc_start(void *address) { ++starts; assert((uintptr_t)address==dma_address); return 0; }
int sdadc_exit(void) { ++stops; channel=0; return 0; }
extern void sdadc_dma_notice(uint32_t event);
void sdadc_isr(void) { ++irqs; sdadc_dma_notice(1); }
static void notice(uint32_t e) { assert(e==1); ++notices; }

static bool dac_ready, dac_active;
static uint32_t accepted_limit;
static unsigned dac_stops, vol, mute, fm_calls;
bool dac_output_init(void) { if (!dac_ready || dac_active) return false; dac_active=true; return true; }
bool dac_output_active(void) { return dac_active; }
void dac_output_stop(void) { assert(dac_active); ++dac_stops; dac_active=false; }
uint32_t dac_output_write(const int16_t *buf, uint32_t frames) { assert(dac_active); return !buf?0:(frames<accepted_limit?frames:accepted_limit); }
bool dac_output_set_volume(uint8_t percent) { vol=percent; return true; }
bool dac_output_mute(bool enabled) { mute=enabled; return true; }
bool drv_usb_audio_init(void) { return false; }
bool drv_usb_audio_send(int16_t *buf, uint32_t count) { (void)buf; (void)count; assert(0); return false; }
void drv_usb_audio_set_volume(uint8_t v) { (void)v; assert(0); }
void drv_usb_audio_mute(bool m) { (void)m; assert(0); }
void fmrx_dma_to_aubuf(uint8_t enabled) { assert(enabled<=1); ++fm_calls; }

int main(void)
{
    drv_adc_cfg_t cfg={.sample_rate=16000,.gain=0x168,.channel=DRV_ADC_CH_MIC};
    _Alignas(4) uint32_t buffer[64];
    assert(!drv_adc_start());
    assert(!drv_adc_start_dma(buffer,sizeof buffer));
    assert(!drv_adc_set_dma_samples(64));
    drv_adc_set_dma_notice_callback(notice);
    sdadc_dma_notice(1); drv_adc_isr(); drv_adc_stop();
    assert(notices==0 && irqs==0 && stops==0);
    assert(!drv_adc_init(NULL));
    cfg.channel=DRV_ADC_CH_AUX; assert(!drv_adc_init(&cfg));
    cfg.channel=DRV_ADC_CH_MIC; cfg.sample_rate=38400; assert(!drv_adc_init(&cfg));
    cfg.sample_rate=16000; cfg.gain=24u<<5; assert(!drv_adc_init(&cfg));
    cfg.gain=0x168; assert(!drv_adc_init(&cfg));
    assert(adc_calls==0);
#ifndef DEFAULT_PLATFORM
    prepared=true; adc_failure=true;
    assert(!drv_adc_init(&cfg)); assert(stops==1 && releases==1);
    adc_failure=false;
    assert(drv_adc_init(&cfg)); assert(channel==0xc);
    assert(!drv_adc_init(&cfg));
    assert(!drv_adc_start_dma(buffer,sizeof buffer));
    assert(!drv_adc_set_dma_samples(0));
    assert(drv_adc_set_dma_samples(64));
    assert(drv_adc_get_dma_samples()==64);
    assert(!drv_adc_start());
    assert(!drv_adc_start_dma(buffer,sizeof buffer));
    mapping=true;
    assert(!drv_adc_start_dma(NULL,sizeof buffer));
    assert(!drv_adc_start_dma((char*)buffer+1,sizeof buffer-1));
    assert(!drv_adc_start_dma(buffer,sizeof buffer-1));
    dma_address=0x12001; assert(!drv_adc_start_dma(buffer,sizeof buffer));
    dma_address=0; assert(!drv_adc_start_dma(buffer,sizeof buffer));
    dma_address=0x12000; assert(drv_adc_start_dma(buffer,sizeof buffer));
    assert(starts==1);
    assert(!drv_adc_start_dma(buffer,sizeof buffer));
    assert(!drv_adc_set_dma_samples(32));
    drv_adc_isr(); assert(notices==1 && irqs==1);
    assert(drv_adc_read((int16_t*)buffer,64)==0);
    drv_adc_stop(); assert(stops==2 && releases==2);
    sdadc_dma_notice(1); drv_adc_isr(); assert(notices==1 && irqs==1);
    assert(!drv_adc_start_dma(buffer,sizeof buffer));
    assert(drv_adc_get_dma_samples()==0);
    assert(drv_adc_init(&cfg));
    assert(!drv_adc_start_dma(buffer,sizeof buffer));
    drv_adc_stop();
#else
    (void)prepared; (void)mapping; (void)releases;
#endif
    int16_t pcm[8]={0};
    assert(!drv_audio_init());
    assert(!drv_audio_set_route(DRV_AUDIO_ROUTE_USB_AUDIO));
    assert(!drv_audio_set_route((drv_audio_route_t)999));
    assert(drv_audio_write_frames(pcm,4)==0);
    assert(!drv_audio_write(pcm,8));
    dac_ready=true;
    assert(drv_audio_init());
    assert(!drv_audio_init());
    assert(!drv_audio_set_route(DRV_AUDIO_ROUTE_FM_TO_BUFFER));
    assert(!drv_audio_set_route(DRV_AUDIO_ROUTE_USB_AUDIO));
    assert(fm_calls==0);
    accepted_limit=2;
    assert(drv_audio_write_frames(pcm,4)==2);
    assert(!drv_audio_write(pcm,8));
    drv_audio_set_volume(10); drv_audio_mute(true); assert(vol==10 && mute);
    assert(drv_audio_set_route(DRV_AUDIO_ROUTE_NONE)); assert(dac_stops==1);
    assert(drv_audio_write_frames(pcm,4)==0);
    assert(drv_audio_set_route(DRV_AUDIO_ROUTE_FM_TO_BUFFER));
    assert(!drv_audio_init());
    drv_audio_stop(); assert(fm_calls==2);
    assert(drv_audio_set_route(DRV_AUDIO_ROUTE_DAC));
    drv_audio_stop(); assert(dac_stops==2);
    puts("PASS audio wrappers: ADC DMA ABI/lifecycle/validation and DAC partial frames/routes");
}
