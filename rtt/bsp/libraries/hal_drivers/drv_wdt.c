/*
 * Copyright (c) 2020-2021, Bluetrum Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2020-11-30     greedyhao         first version
 */

#include <board.h>

#ifdef RT_USING_WDT

#include <drv_wdt.h>

// #define DRV_DEBUG
#define LOG_TAG             "drv.wdt"
#include <drv_log.h>

struct ab32_wdt_obj
{
    rt_watchdog_t watchdog;
};
static struct ab32_wdt_obj ab32_wdt;
static struct rt_watchdog_ops ops;

static rt_err_t wdt_init(rt_watchdog_t *wdt)
{
    return RT_EOK;
}

static rt_err_t wdt_control(rt_watchdog_t *wdt, int cmd, void *arg)
{
    rt_uint32_t seconds, select;
    switch (cmd)
    {
    case RT_DEVICE_CTRL_WDT_KEEPALIVE:
        WDTCON = 0xa;
        break;
    case RT_DEVICE_CTRL_WDT_SET_TIMEOUT:
        if (!arg)
            return -RT_EINVAL;
        seconds = *(rt_uint32_t *)arg;
        if (seconds < 1 || seconds > 16)
            return -RT_EINVAL;
        /* RT-Thread uses seconds, not the hardware's 0..7 encoding.
         * Round up to 1.024/2.048/4.096/8.192/16.384 seconds. */
        for (select = 3; (1u << (select - 3)) < seconds; ++select) {}
        WDTCON = (0xau << 24) | (select << 20);
        break;
    case RT_DEVICE_CTRL_WDT_GET_TIMEOUT:
        if (!arg)
            return -RT_EINVAL;
        select = (WDTCON >> 20) & 7;
        if (select < 3)
            return -RT_ENOSYS; /* subsecond setting cannot be reported */
        *(rt_uint32_t *)arg = 1u << (select - 3);
        break;
    case RT_DEVICE_CTRL_WDT_START:
        WDTCON = 0x11a;
        break;
    case RT_DEVICE_CTRL_WDT_STOP:
        WDTCON = 0xaa0;
        break;
    default:
        return -RT_ENOSYS;
    }
    return RT_EOK;
}

int rt_wdt_init(void)
{
    ops.init = &wdt_init;
    ops.control = &wdt_control;
    ab32_wdt.watchdog.ops = &ops;
    /* register watchdog device */
    if (rt_hw_watchdog_register(&ab32_wdt.watchdog, "wdt", RT_DEVICE_FLAG_DEACTIVATE, RT_NULL) != RT_EOK)
    {
        LOG_E("wdt device register failed.");
        return -RT_ERROR;
    }
    LOG_D("wdt device register success.");
    return RT_EOK;
}
INIT_BOARD_EXPORT(rt_wdt_init);

#endif /* RT_USING_WDT */
