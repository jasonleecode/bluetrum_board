#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "board.h"
volatile uint32_t test_sd[10];
static int event_call, fail_command, fail_data, data_crc, dma_calls, completed;
uint32_t get_sysclk_nhz(void) { return 48000000; }
int rt_kprintf(const char *fmt, ...) { (void)fmt; return 0; }
void rt_assert_handler(const char *ex, const char *func, rt_size_t line) {
    fprintf(stderr, "%s:%lu %s\n", func, (unsigned long)line, ex); abort();
}
rt_tick_t rt_tick_from_millisecond(rt_int32_t ms) { return ms; }
void *rt_memset(void *p, int c, rt_ubase_t n) { return memset(p, c, n); }
void *rt_memcpy(void *d, const void *s, rt_ubase_t n) { return memcpy(d, s, n); }
rt_err_t rt_mutex_take(rt_mutex_t m, rt_int32_t t) { (void)m; (void)t; return RT_EOK; }
rt_err_t rt_mutex_release(rt_mutex_t m) { (void)m; return RT_EOK; }
rt_err_t rt_event_control(rt_event_t e, int cmd, void *arg) {
    (void)e; (void)cmd; (void)arg; event_call = 0; return RT_EOK;
}
#include "../../../rtt/bsp/libraries/hal_drivers/drv_sdio.c"
static struct rthw_sdio controller;
rt_err_t rt_event_recv(rt_event_t e, rt_uint32_t set, rt_uint8_t opt,
                       rt_int32_t timeout, rt_uint32_t *received) {
    (void)e; (void)opt; (void)timeout;
    if (event_call++ == 0) {
        assert(set == HW_SDIO_CON_CFLAG);
        controller.command_status = HW_SDIO_CON_CFLAG;
        if (fail_command) return -RT_ETIMEOUT;
    } else {
        assert(set == HW_SDIO_CON_DFLAG);
        if (fail_data) return -RT_ETIMEOUT;
        controller.data_status = HW_SDIO_CON_DFLAG | (2u << 17);
        if (data_crc) controller.data_status |= HW_SDIO_CON_DCRCE;
    }
    *received = set;
    return RT_EOK;
}
void mmcsd_req_complete(struct rt_mmcsd_host *h) { (void)h; completed++; }
static rt_err_t dma(rt_uint32_t *p, int size) { (void)p; assert(size == 512); dma_calls++; return RT_EOK; }
int main(void) {
    struct rt_mmcsd_host h = {0};
    struct rt_mmcsd_cmd cmd = {.cmd_code = 17, .flags = RESP_R1};
    uint32_t buf[256] = {0};
    struct rt_mmcsd_data data = {.buf = buf, .blksize = 512, .blks = 1, .flags = DATA_DIR_READ};
    struct rt_mmcsd_req req = {.cmd = &cmd};
    h.private_data = &controller; controller.host = &h;
    controller.sdio_des.hw_sdio = test_sd;
    controller.sdio_des.rxconfig = dma; controller.sdio_des.txconfig = dma;
    cmd.data = &data;
    rthw_sdio_request(&h, &req);
    assert(cmd.err == RT_EOK && data.err == RT_EOK && event_call == 2 && completed == 1);
    fail_command = 1;
    rthw_sdio_request(&h, &req);
    assert(cmd.err == -RT_ETIMEOUT && data.err == -RT_ETIMEOUT && event_call == 1);
    fail_command = 0; fail_data = 1;
    rthw_sdio_request(&h, &req);
    assert(cmd.err == -RT_ETIMEOUT && event_call == 2);
    fail_data = 0; data_crc = 1;
    rthw_sdio_request(&h, &req);
    assert(cmd.err == -RT_EIO && data.err == -RT_EIO);
    data_crc = 0; data.blks = 3; dma_calls = 0;
    rthw_sdio_request(&h, &req);
    assert(cmd.err == -RT_EINVAL && dma_calls == 0);
    data.blks = 2; data.flags = DATA_DIR_WRITE;
    rthw_sdio_request(&h, &req);
    assert(cmd.err == RT_EOK && dma_calls == 2 && event_call == 3);
    puts("PASS: SD command/data event separation, timeout/CRC propagation, buffer bounds, multi-block writes");
}
