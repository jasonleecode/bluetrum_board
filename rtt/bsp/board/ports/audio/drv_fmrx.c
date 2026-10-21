/* SPDX-License-Identifier: Apache-2.0
 * RT-Thread lifecycle around the bare recovered FM implementation.
 * Audio route/sync count follows docs/ab32vg1_doc/test_program/fmrx_test1.c.
 */
#include <board.h>
#include <stdlib.h>
#include "drv_fmrx.h"
#include "fmrx_hal.h"

static struct rt_device fm_device;
static struct rt_mutex fm_lock;
static struct ab32_fmrx_status fm_status = {0, 55, RT_FALSE, RT_EOK};
static rt_uint32_t fm_buffer_words;
static rt_err_t pll_error;

/* Called only while fm_lock is held. All callers check the sticky error and
 * power down before enabling the FM -> audio route if calibration times out.
 * The default bare build keeps its original indefinite calibration wait.
 */
int ab32_fmrx_wait_pll(volatile uint32_t *status)
{
    rt_tick_t start = rt_tick_get();
    rt_tick_t timeout = rt_tick_from_millisecond(100);
    while (!(*status & 0x80u))
    {
        if ((rt_tick_t)(rt_tick_get() - start) >= timeout)
        {
            pll_error = -RT_ETIMEOUT;
            return -1;
        }
        rt_thread_mdelay(1);
    }
    return 0;
}

static rt_bool_t fm_frequency_valid(rt_uint32_t khz)
{
    return khz >= 76000 && khz <= 108000 && khz % 10 == 0;
}

/* Also used to unwind a partial power-on or failed retune. Caller owns DAC. */
static void fm_shutdown(void)
{
    ab32_sound_fm_volume(0);
    fmrx_power_off(); /* stops RF and DMA before the sound buffer is released */
    ab32_sound_fm_release();
    hal_rcu_periph_clk_disable(RCU_FMRX);
    fm_status.running = RT_FALSE;
    fm_status.frequency_khz = 0;
}

static rt_err_t fm_control(rt_device_t dev, int cmd, void *arg)
{
    rt_uint32_t value;
    rt_err_t ret;
    (void)dev;
    if (cmd != AB32_FMRX_STOP && !arg)
        return -RT_EINVAL;
    ret = rt_mutex_take(&fm_lock, RT_WAITING_FOREVER);
    if (ret != RT_EOK)
        return ret;
    switch (cmd)
    {
    case AB32_FMRX_START:
    case AB32_FMRX_TUNE:
        value = *(rt_uint32_t *)arg;
        if (!fm_frequency_valid(value))
        { ret = -RT_EINVAL; break; }
        if (cmd == AB32_FMRX_START)
        {
            if (fm_status.running)
            { ret = -RT_EBUSY; break; }
            ret = ab32_sound_fm_acquire(&fm_buffer_words);
            if (ret != RT_EOK)
                break;
            hal_rcu_periph_clk_enable(RCU_FMRX);
            pll_error = RT_EOK;
            fmrx_power_on(0);
        }
        else
        {
            if (!fm_status.running)
            { ret = -RT_EIO; break; }
            ab32_sound_fm_volume(0);
            pll_error = RT_EOK;
        }
        if (pll_error == RT_EOK)
            fmrx_set_pll(1, value / 10);
        if (pll_error != RT_EOK)
        {
            ret = pll_error;
            fm_shutdown();
            break;
        }
        fmrx_dma_to_aubuf(1);
        fm_status.frequency_khz = value;
        fm_status.running = RT_TRUE;
        ab32_sound_fm_volume(fm_status.volume);
        break;
    case AB32_FMRX_STOP:
        if (fm_status.running)
            fm_shutdown();
        break;
    case AB32_FMRX_VOLUME:
        value = *(rt_uint32_t *)arg;
        if (value > 100)
        { ret = -RT_EINVAL; break; }
        fm_status.volume = value;
        if (fm_status.running)
            ab32_sound_fm_volume(value);
        break;
    case AB32_FMRX_STATUS:
        *(struct ab32_fmrx_status *)arg = fm_status;
        break;
    default:
        ret = -RT_ENOSYS;
        break;
    }
    if (cmd != AB32_FMRX_STATUS)
        fm_status.last_error = ret;
    rt_mutex_release(&fm_lock);
    return ret;
}

static void fm_sync_once(void)
{
    rt_mutex_take(&fm_lock, RT_WAITING_FOREVER);
    if (fm_status.running)
        fmrx_dac_sync(fm_buffer_words);
    rt_mutex_release(&fm_lock);
}

static void fm_sync_entry(void *arg)
{
    (void)arg;
    while (1)
    {
        fm_sync_once();
        rt_thread_mdelay(100);
    }
}

#ifdef RT_USING_DEVICE_OPS
static const struct rt_device_ops fm_ops = { .control = fm_control };
#endif
static int rt_hw_fmrx_init(void)
{
    rt_thread_t worker;
    rt_err_t ret;
    if (!rt_device_find("sound0"))
        return -RT_ENOSYS;
    ret = rt_mutex_init(&fm_lock, "fmlock", RT_IPC_FLAG_PRIO);
    if (ret != RT_EOK)
        return ret;
    worker = rt_thread_create("fm_sync", fm_sync_entry, RT_NULL, 1024, 21, 10);
    if (!worker)
    { rt_mutex_detach(&fm_lock); return -RT_ENOMEM; }
    fm_device.type = RT_Device_Class_Miscellaneous;
#ifdef RT_USING_DEVICE_OPS
    fm_device.ops = &fm_ops;
#else
    fm_device.control = fm_control;
#endif
    ret = rt_device_register(&fm_device, "fm0", RT_DEVICE_FLAG_RDWR);
    if (ret == RT_EOK)
    {
        ret = rt_thread_startup(worker);
        if (ret == RT_EOK)
            return ret;
        rt_device_unregister(&fm_device);
    }
    rt_thread_delete(worker);
    rt_mutex_detach(&fm_lock);
    return ret;
}
/* sound0 is registered at DEVICE stage. */
INIT_ENV_EXPORT(rt_hw_fmrx_init);

#ifdef RT_USING_FINSH
static int fmrx(int argc, char **argv)
{
    rt_device_t dev = rt_device_find("fm0");
    rt_uint32_t value;
    int command;
    rt_err_t ret;
    char *end;
    unsigned long parsed;
    if (!dev)
    { rt_kprintf("fm0 unavailable\n"); return -RT_ENOSYS; }
    if (argc == 2 && !rt_strcmp(argv[1], "status"))
    {
        struct ab32_fmrx_status status;
        ret = rt_device_control(dev, AB32_FMRX_STATUS, &status);
        if (ret == RT_EOK)
            rt_kprintf("FM %s, %u kHz, volume %u, last error %d\n",
                status.running ? "running" : "off", status.frequency_khz,
                status.volume, (int)status.last_error);
        return ret;
    }
    if (argc == 2 && !rt_strcmp(argv[1], "stop"))
        ret = rt_device_control(dev, AB32_FMRX_STOP, RT_NULL);
    else
    {
        if (argc != 3)
            goto usage;
        if (!rt_strcmp(argv[1], "start")) command = AB32_FMRX_START;
        else if (!rt_strcmp(argv[1], "tune")) command = AB32_FMRX_TUNE;
        else if (!rt_strcmp(argv[1], "volume")) command = AB32_FMRX_VOLUME;
        else goto usage;
        if (argv[2][0] < '0' || argv[2][0] > '9') goto usage;
        parsed = strtoul(argv[2], &end, 10);
        if (*end || parsed > 108000) goto usage;
        value = parsed;
        ret = rt_device_control(dev, command, &value);
    }
    if (ret != RT_EOK) rt_kprintf("FM error %d\n", (int)ret);
    return ret;
usage:
    rt_kprintf("fmrx start|tune <76000..108000 kHz, step 10>\n"
               "fmrx volume <0..100> | stop | status\n");
    return -RT_EINVAL;
}
MSH_CMD_EXPORT(fmrx, FM receiver manual tuning and DAC playback);
#endif
