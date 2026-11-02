/*
 * Copyright (c) 2020-2021, Bluetrum Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * Native IRTC seconds driver. No alarm/second IRQ or power-down wake support.
 * RTC bridge transactions and every callee with IRQs masked must stay in SRAM.
 */
#include "board.h"
#include <time.h>
#ifdef RT_USING_FINSH
#include <finsh.h>
#include <string.h>
#endif

#ifdef BSP_USING_ONCHIP_RTC
#define RTC_RAM rt_section(".irq.rtc") __attribute__((noinline))
/* Bounded register polls, not OS ticks: the tick IRQ is masked here. */
#define RTC_POLL_LIMIT 1024u
#ifdef AB32_RTC_TEST
extern rt_uint32_t rtc_test_control(void);
extern void rtc_test_write(rt_uint32_t value);
#define RTC_READ_CONTROL() rtc_test_control()
#define RTC_WRITE_DATA(v) rtc_test_write(v)
#else
#define RTC_READ_CONTROL() RTCCON
#define RTC_WRITE_DATA(v) (RTCDAT = (v))
#endif

static rt_rtc_dev_t ab32_rtc_dev;
static struct rt_mutex rtc_lock;
static rt_bool_t rtc_ready, rtc_initialized, rtc_uncertain;

RTC_RAM static rt_err_t rtc_wait_idle(void)
{
    for (unsigned i = 0; i < RTC_POLL_LIMIT; i++)
        if (!(RTC_READ_CONTROL() & RTC_CON_TRANS_DONE)) return RT_EOK;
    return -RT_ETIMEOUT; /* RTCPND=1 means busy, despite the old macro name. */
}
RTC_RAM static rt_err_t rtc_byte(rt_uint32_t value)
{
    RTC_WRITE_DATA(value);
    return rtc_wait_idle();
}
/* Caller owns rtc_lock. Values are assembled explicitly, MSB first. Failed
 * reads leave the destination unchanged; all exits release CS and restore IRQs. */
RTC_RAM static rt_err_t rtc_transfer(rt_uint32_t cmd, rt_uint32_t *value,
                                    unsigned bytes, rt_bool_t write)
{
    rt_uint32_t result = 0;
    rt_base_t level = rt_hw_interrupt_disable();
    rt_err_t rc = rtc_wait_idle();
    if (rc) goto out;
    RTCCON |= RTC_CON_CHIP_SELECT;
    rc = rtc_byte(cmd | (write ? RTC_WR : RTC_RD));
    for (unsigned i = 0; !rc && i < bytes; i++)
    {
        rc = rtc_byte(write ? (*value >> (8 * (bytes - i - 1))) & 0xffu : 0);
        if (!rc && !write) result = (result << 8) | (RTCDAT & 0xffu);
    }
out:
    RTCCON &= ~RTC_CON_CHIP_SELECT;
    rt_hw_interrupt_enable(level);
    if (!rc && !write) *value = result;
    return rc;
}
RTC_RAM static void rtc_bridge_init(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    /* /32 bridge clock; preserve wake/reset policy. No unhandled RTC IRQs. */
    RTCCON = (RTCCON & ~(RTC_CON_ALM_INTERRUPT | RTC_CON_1S_INTERRUPT |
                        RTC_CON_CHIP_SELECT)) |
             RTC_CON_BAUD_SELECT;
    rt_hw_interrupt_enable(level);
}
static rt_err_t rtc_take(void)
{
    if (!rtc_ready) return -RT_EIO;
    if (rt_interrupt_get_nest() || !rt_thread_self()) return -RT_EBUSY;
    return rt_mutex_take(&rtc_lock, RT_WAITING_FOREVER);
}
/* Lazy initialization also covers rt_device_control() without prior open(). */
static rt_err_t rtc_ensure_init(void)
{
    rt_uint32_t con0, con2;
    if (rtc_initialized) return RT_EOK;
    rtc_bridge_init();
    rt_err_t rc = rtc_transfer(RTCCON0_CMD, &con0, 1, RT_FALSE);
    if (rc) return rc;
    rc = rtc_transfer(RTCCON2_CMD, &con2, 1, RT_FALSE);
    if (rc) return rc;
#ifdef RTC_USING_INTERNAL_CLK
    /* Match the AB32 BSP internal clock route. V02 hardware: clearing bit6
     * stops RTCCNT; setting it resumes ~1 Hz. The manual's EXT32KS label does
     * not mean this board requires an external crystal for the bit=1 path. */
    con0 = (con0 & ~RTC_CON0_XOSC32K_ENABLE) | RTC_CON0_32K_SOURCE_SELECT |
           RTC_CON0_RCOSC_ENABLE | RTC_CON0_VDD_ENABLE;
    con2 |= RTC_CON2_32K_SELECT;
#else
    con0 = (con0 & ~(RTC_CON0_RCOSC_ENABLE | RTC_CON0_32K_SOURCE_SELECT)) |
           RTC_CON0_XOSC32K_ENABLE | RTC_CON0_VDD_ENABLE;
    con2 &= ~RTC_CON2_32K_SELECT;
#endif
    /* Preserve power-up flag, count, alarm, RTC RAM and reserved/power bits. */
    rc = rtc_transfer(RTCCON0_CMD, &con0, 1, RT_TRUE);
    if (!rc) rc = rtc_transfer(RTCCON2_CMD, &con2, 1, RT_TRUE);
    if (!rc) rtc_initialized = RT_TRUE;
    return rc;
}
static rt_err_t ab32_rtc_init(void)
{
    rt_err_t rc = rtc_take();
    if (rc) return rc;
    rc = rtc_ensure_init();
    rt_mutex_release(&rtc_lock);
    return rc;
}
/* The manual does not promise a latch across the four byte reads. Accept only
 * consecutive equal/adjacent samples; retry rollover-torn values, bounded. */
static rt_err_t rtc_counter_read(rt_uint32_t *value)
{
    rt_uint32_t previous, current;
    rt_err_t rc = rtc_transfer(RTCCNT_CMD, &previous, 4, RT_FALSE);
    for (unsigned i = 0; !rc && i < 3; i++)
    {
        rc = rtc_transfer(RTCCNT_CMD, &current, 4, RT_FALSE);
        if (rc) return rc;
        if (current == previous || current == (rt_uint32_t)(previous + 1u))
        {
            *value = current;
            return RT_EOK;
        }
        previous = current;
    }
    return rc ? rc : -RT_EIO;
}
static rt_err_t ab32_rtc_get_secs(time_t *secs)
{
    rt_uint32_t con0, value;
    if (!secs) return -RT_EINVAL;
    rt_err_t rc = rtc_take();
    if (rc) return rc;
    rc = rtc_ensure_init();
    if (!rc) rc = rtc_transfer(RTCCON0_CMD, &con0, 1, RT_FALSE);
    if (!rc && ((con0 & RTC_CON0_PWRUP_FIRST) || rtc_uncertain)) rc = -RT_EIO;
    if (!rc) rc = rtc_counter_read(&value);
    if (!rc)
    {
        time_t converted = (time_t)value;
        if (converted < 0 || (rt_uint64_t)converted != value) rc = -RT_EINVAL;
        else *secs = converted;
    }
    rt_mutex_release(&rtc_lock);
    return rc;
}
static rt_err_t ab32_rtc_set_secs(time_t *secs)
{
    rt_uint32_t con0, value;
    if (!secs || *secs < 0 || (rt_uint64_t)*secs > 0xffffffffULL) return -RT_EINVAL;
    value = (rt_uint32_t)*secs;
    rt_err_t rc = rtc_take();
    if (rc) return rc;
    rc = rtc_ensure_init();
    if (!rc) rc = rtc_transfer(RTCCON0_CMD, &con0, 1, RT_FALSE);
    if (!rc)
    {
        /* Mark time unset before a potentially partial four-byte write. Only
         * clear the documented R/W flag once every transfer has succeeded. */
        rtc_uncertain = RT_TRUE;
        con0 |= RTC_CON0_PWRUP_FIRST;
        rc = rtc_transfer(RTCCON0_CMD, &con0, 1, RT_TRUE);
        if (!rc) rc = rtc_transfer(RTCCNT_CMD, &value, 4, RT_TRUE);
        if (!rc)
        {
            con0 &= ~RTC_CON0_PWRUP_FIRST;
            rc = rtc_transfer(RTCCON0_CMD, &con0, 1, RT_TRUE);
        }
        if (!rc) rtc_uncertain = RT_FALSE;
    }
    rt_mutex_release(&rtc_lock);
    return rc;
}
static const struct rt_rtc_ops ab32_rtc_ops = {
    .init = ab32_rtc_init, .get_secs = ab32_rtc_get_secs, .set_secs = ab32_rtc_set_secs,
};
static int rt_hw_rtc_init(void)
{
    rt_err_t rc = rt_mutex_init(&rtc_lock, "rtc", RT_IPC_FLAG_PRIO);
    if (rc) return rc;
    ab32_rtc_dev.ops = &ab32_rtc_ops;
    rc = rt_hw_rtc_register(&ab32_rtc_dev, "rtc", RT_DEVICE_FLAG_RDWR, RT_NULL);
    if (rc) { rt_mutex_detach(&rtc_lock); return rc; }
    rtc_ready = RT_TRUE;
    return RT_EOK;
}
INIT_DEVICE_EXPORT(rt_hw_rtc_init);

#ifdef RT_USING_FINSH
/* The stock date command reads old time before setting it, so an unset RTC
 * needs this direct setter (or set_timestamp()) for its first valid UTC time. */
static int rtcctl(int argc, char **argv)
{
    rt_err_t rc;
    time_t seconds;
    if (argc == 2 && !strcmp(argv[1], "status"))
    {
        rt_uint32_t con0 = 0, con2 = 0, count = 0;
        rc = rtc_take();
        if (rc) return rc;
        rc = rtc_ensure_init();
        if (!rc) rc = rtc_transfer(RTCCON0_CMD, &con0, 1, RT_FALSE);
        if (!rc) rc = rtc_transfer(RTCCON2_CMD, &con2, 1, RT_FALSE);
        if (!rc) rc = rtc_counter_read(&count);
        rt_bool_t valid = !(con0 & RTC_CON0_PWRUP_FIRST) && !rtc_uncertain;
        rt_mutex_release(&rtc_lock);
        if (!rc) rt_kprintf("RTC CON0=%02x CON2=%02x count=%u valid=%u time_t_bits=%u\n",
                            con0, con2, count, valid, (unsigned)(sizeof(time_t) * 8));
    }
    else if (argc == 2 && !strcmp(argv[1], "get"))
    {
        rc = ab32_rtc_get_secs(&seconds);
        if (!rc) rt_kprintf("RTC UTC seconds=%u\n", (unsigned)seconds);
    }
    else if (argc == 3 && !strcmp(argv[1], "set"))
    {
        rt_uint64_t parsed = 0;
        const char *p = argv[2];
        if (!*p) return -RT_EINVAL;
        for (; *p; p++)
        {
            if (*p < '0' || *p > '9') return -RT_EINVAL;
            parsed = parsed * 10 + (*p - '0');
            if (parsed > 0xffffffffULL) return -RT_EINVAL;
        }
        seconds = (time_t)parsed;
        rc = ab32_rtc_set_secs(&seconds);
    }
    else
    {
        rt_kprintf("rtcctl status | get | set <UTC Unix seconds, limited by time_t>\n");
        return -RT_EINVAL;
    }
    if (rc) rt_kprintf("RTC error %d (unset time, busy context or transfer failure)\n", rc);
    return rc;
}
MSH_CMD_EXPORT(rtcctl, Read or set native RTC UTC seconds);
#endif
#endif /* BSP_USING_ONCHIP_RTC */
