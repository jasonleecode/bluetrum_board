#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include "drv_fm.h"
#include "fmrx_hal.h"

enum operation { POWER, OFF, START, STOP, PLL, CAP, DMA, ROUTE, SYNC };
struct call { enum operation op; uintptr_t a; uint32_t b; };
static struct call calls[64];
static unsigned count;

static void record(enum operation op, uintptr_t a, uint32_t b)
{
    assert(count < sizeof(calls) / sizeof(calls[0]));
    calls[count++] = (struct call){op, a, b};
}
static void expect(unsigned i, enum operation op, uintptr_t a, uint32_t b)
{
    assert(i < count && calls[i].op == op && calls[i].a == a && calls[i].b == b);
}
void fmrx_power_on(uint32_t v) { record(POWER, v, 0); }
void fmrx_power_off(void) { record(OFF, 0, 0); }
void fmrx_digital_start(void) { record(START, 0, 0); }
void fmrx_digital_stop(void) { record(STOP, 0, 0); }
void fmrx_set_pll(uint32_t mode, uint32_t freq) { record(PLL, mode, freq); }
void fmrx_set_rf_cap(uint32_t v) { record(CAP, v, 0); }
void fmrx_get_audio_data(void *buf, uint32_t n) { record(DMA, (uintptr_t)buf, n); }
void fmrx_dma_to_aubuf(uint8_t v) { record(ROUTE, v, 0); }
void fmrx_dac_sync(uint32_t n) { record(SYNC, n, 0); }

int main(void)
{
    int16_t buf[32];
    drv_fm_cfg_t cfg = {101100, 16};
    assert(!drv_fm_start());
    assert(!drv_fm_tune(101100));
    assert(!drv_fm_capture_raw(buf, 8));
    assert(!drv_fm_route_to_audio_buffer(true));
    drv_fm_sync_dac(512);
    drv_fm_stop();
    drv_fm_power_off();
    assert(!drv_fm_init(NULL));
    const uint32_t invalid[] = {0, 75990, 108010, 101101, UINT32_MAX};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        cfg.frequency_khz = invalid[i];
        assert(!drv_fm_init(&cfg));
    }
    cfg.frequency_khz = 101100;
    cfg.rf_cap = 32;
    assert(!drv_fm_init(&cfg));
    assert(count == 0);

    cfg.rf_cap = 16;
    assert(drv_fm_init(&cfg));
    assert(count == 3);
    expect(0, POWER, 0, 0);
    expect(1, PLL, 1, 10110); /* Regression: two arguments, 10-kHz units. */
    expect(2, CAP, 16, 0);
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        assert(!drv_fm_tune(invalid[i]));
    assert(!drv_fm_capture_raw(NULL, 8));
    assert(!drv_fm_capture_raw(buf, 0));
    assert(count == 3);

    assert(drv_fm_tune(76000));
    assert(drv_fm_tune(108000));
    expect(3, PLL, 1, 7600);
    expect(4, PLL, 1, 10800);
    assert(drv_fm_start());
    expect(5, START, 0, 0);
    assert(drv_fm_capture_raw(buf, 8));
    assert(drv_fm_capture_to_buffer(buf, 16));
    assert(drv_fm_get_audio(buf, 4) == 0);
    expect(6, DMA, (uintptr_t)buf, 8);
    expect(7, DMA, (uintptr_t)buf, 16);
    expect(8, DMA, (uintptr_t)buf, 4);
    assert(drv_fm_route_to_audio_buffer(true));
    assert(drv_fm_route_to_audio_buffer(false));
    expect(9, ROUTE, 1, 0);
    expect(10, ROUTE, 0, 0);
    drv_fm_sync_dac(512);
    expect(11, SYNC, 512, 0);
    drv_fm_stop();
    expect(12, STOP, 0, 0);
    drv_fm_power_off();
    expect(13, OFF, 0, 0);
    assert(!drv_fm_start());
    assert(!drv_fm_tune(87500));
    assert(!drv_fm_capture_raw(buf, 8));
    drv_fm_power_off();
    assert(count == 14);
    puts("PASS FM wrapper: ABI, frequency limits, DMA guards and lifecycle");
    return 0;
}
