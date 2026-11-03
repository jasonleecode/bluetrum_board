/*
 * Copyright (c) 2020-2021, Bluetrum Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * Native IRTC seconds and serialized PM bridge. No alarm/second IRQ driver.
 * RTC bridge transactions and every callee with IRQs masked must stay in SRAM.
 */
#include "board.h"
#include <time.h>
#ifdef BSP_USING_POWER
#include "drv_rtc_internal.h"
#endif
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
RTC_RAM static void rtc_bridge_init(rt_bool_t rtc_device)
{
    rt_base_t level = rt_hw_interrupt_disable();
    /* /32 bridge clock; preserve wake/reset policy. No unhandled RTC IRQs. */
    rt_uint32_t clear = RTC_CON_CHIP_SELECT;
    if (rtc_device) clear |= RTC_CON_ALM_INTERRUPT | RTC_CON_1S_INTERRUPT;
    RTCCON = (RTCCON & ~clear) | RTC_CON_BAUD_SELECT;
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
    rtc_bridge_init(RT_TRUE);
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
RTC_RAM static rt_err_t rtc_counter_read(rt_uint32_t *value)
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

#ifdef BSP_USING_POWER
/* Capture a seconds boundary while scheduling is still allowed. Without
 * alignment, second-resolution RTC compensation could add almost an extra
 * second on every short sleep. Never change the counter/calendar validity. */
rt_err_t ab32_irtc_align_second_locked(rt_uint32_t *second, rt_tick_t *edge)
{
    rt_uint32_t initial, now;
    rt_err_t rc = rtc_ensure_init();
    if (!rc) rc = rtc_counter_read(&initial);
    if (rc) return rc;
    rt_tick_t start = rt_tick_get();
    do
    {
        rt_thread_mdelay(1);
        rc = rtc_counter_read(&now);
        if (rc) return rc;
        if (now != initial)
        {
            if ((rt_uint32_t)(now - initial) != 1) return -RT_EIO;
            *second = now; *edge = rt_tick_get();
            return RT_EOK;
        }
    } while ((rt_tick_t)(rt_tick_get() - start) < 2u * RT_TICK_PER_SECOND);
    return -RT_ETIMEOUT; /* stopped RTC: do not enter hardware sleep */
}

RTC_RAM rt_err_t ab32_irtc_sleep_locked(rt_uint32_t second, rt_tick_t edge, rt_uint32_t *entries)
{
    rt_base_t level = rt_hw_interrupt_disable();
    rt_uint32_t bridge = RTCCON, timer = TMR0CON, lowpower = LPMCON, now;
    rt_err_t rc = rtc_counter_read(&now);
    if (rc) goto out;
    if (now != second || (rt_tick_t)(rt_tick_get() - edge) > RT_TICK_PER_SECOND / 10u)
    { rc = -RT_EBUSY; goto out; } /* preempted too long after the edge */
    /* Timer0 cannot wake LPMCON sleep on V2 hardware. Use the independently
     * routed RTC 1s source, suppress unhandled RTC IRQ/reset, and stop tick
     * counting explicitly. Other wake sources may return early; continue to
     * the next RTC edge for a measurable interval. */
    TMR0CON = timer & ~1u;
    RTCCON = (RTCCON & ~0x1f8u) | RTC_CON_1S_WK_ENABLE;
#ifdef AB32_RTC_TEST
    extern void power_test_rtc_pending_clear(void);
    extern void power_test_sleep(void);
    power_test_rtc_pending_clear();
#else
    RTCCPND = RTC_CPND_1S;
    __asm__ volatile ("nop; nop; nop" ::: "memory");
#endif
    do
    {
        ++*entries;
#ifdef AB32_RTC_TEST
        power_test_sleep();
#else
        LPMCON |= 1u;
        __asm__ volatile ("nop; nop; nop" ::: "memory");
#endif
        LPMCON = lowpower;
    } while (!(RTCCON & RTC_CON_1S_PEND));
    rc = rtc_counter_read(&now);
    if (!rc)
    {
        rt_uint32_t seconds = now - second;
        if (!seconds || seconds > 2) rc = -RT_EIO;
        else rt_tick_set(edge + seconds * RT_TICK_PER_SECOND);
    }
    /* Expired software/hardware timers are checked by the next normal tick
     * ISR, after IRQs and scheduling are restored. Do not invoke callbacks
     * from the power driver's critical section. Leave the new RTC flag for
     * an existing consumer; restore its original interrupt/wake policy. */
    RTCCON = (RTCCON & ~0x1f8u) | (bridge & 0x1f8u);
    TMR0CON = timer;
out:
    rt_hw_interrupt_enable(level);
    return rc;
}

/* PM status/configuration must not initialize the RTC oscillator, clear its
 * first-power flag, touch time, or change inherited interrupt/reset policy. */
rt_err_t ab32_irtc_transaction(rt_err_t (*operation)(void *), void *arg)
{
    if (!operation) return -RT_EINVAL;
    rt_err_t rc = rtc_take();
    if (rc) return rc;
    rtc_bridge_init(RT_FALSE);
    rc = operation(arg);
    rt_mutex_release(&rtc_lock);
    return rc;
}
rt_err_t ab32_irtc_read8_locked(rt_uint8_t command, rt_uint8_t *value)
{
    rt_uint32_t raw;
    if (!value) return -RT_EINVAL;
    switch (command)
    {
    case RTCCON0_CMD: case RTCCON1_CMD: case RTCCON2_CMD: case RTCCON3_CMD:
    case RTCCON5_CMD: case RTCCON7_CMD: case RTCCON8_CMD: case RTCCON9_CMD:
        break;
    default: return -RT_EINVAL;
    }
    rt_err_t rc = rtc_transfer(command, &raw, 1, RT_FALSE);
    if (!rc) *value = raw;
    return rc;
}
static rt_err_t rtc_update_checked(rt_uint8_t command, rt_uint8_t mask, rt_uint8_t value)
{
    rt_uint8_t old, actual;
    rt_err_t rc = ab32_irtc_read8_locked(command, &old);
    if (rc) return rc;
    rt_uint32_t desired = (old & ~mask) | value;
    if (desired != old) rc = rtc_transfer(command, &desired, 1, RT_TRUE);
    if (!rc) rc = ab32_irtc_read8_locked(command, &actual);
    if (!rc && ((actual ^ desired) & mask)) rc = -RT_EIO;
    return rc;
}
rt_err_t ab32_irtc_update8_locked(rt_uint8_t command, rt_uint8_t mask, rt_uint8_t value)
{
    rt_uint8_t allowed;
    switch (command)
    {
    case RTCCON1_CMD: allowed = 0x13; break; /* input, pull-up/down only */
    case RTCCON3_CMD: allowed = 0x30; break; /* WK/VUSB wake, never rails */
    case RTCCON7_CMD: allowed = 0x07; break; /* filter, never reset/protect */
    default: return -RT_EINVAL;
    }
    if (!mask || (mask & ~allowed) || (value & ~mask)) return -RT_EINVAL;
    return rtc_update_checked(command, mask, value);
}
rt_err_t ab32_irtc_shutdown_update_locked(rt_uint8_t command, rt_uint8_t mask, rt_uint8_t value)
{
    rt_uint8_t allowed;
    switch (command)
    {
    case RTCCON0_CMD: allowed = 4; break;    /* LVD only */
    case RTCCON3_CMD: allowed = 0xf0; break; /* wake policy; never rails here */
    case RTCCON5_CMD: allowed = 3; break;    /* BUCK -> LDO mode */
    default: return -RT_EINVAL;
    }
    if (!mask || (mask & ~allowed) || (value & ~mask)) return -RT_EINVAL;
    return rtc_update_checked(command, mask, value);
}
RTC_RAM void ab32_irtc_cutoff_locked(rt_uint8_t con3)
{
    /* The last rail write is a point of no return. A timeout may mean the
     * write was accepted; never resume threads with uncertain supply state. */
    rt_uint32_t value = con3;
    rt_err_t rc = rtc_transfer(RTCCON3_CMD, &value, 1, RT_TRUE);
#ifdef AB32_RTC_TEST
    extern void power_test_terminal(rt_bool_t reset) __attribute__((noreturn));
    power_test_terminal(rc != RT_EOK);
#else
    if (rc) WDTCON = 0x0a000110; /* BSP WDT_RST encoding */
    else
    {
        WDT_DIS();
        LPMCON |= 1u;
        __asm__ volatile ("nop; nop; nop" ::: "memory");
    }
    for (;;) __asm__ volatile ("nop"); /* reset/wake enters ROM, never returns */
#endif
}
#endif

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
