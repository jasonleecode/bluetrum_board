/* SPDX-License-Identifier: Apache-2.0
 * Thread-only native SD0 -> RTT block device. No automatic probing/formatting.
 * PB0 CMD, PB1 CLK, PB2 DAT0; exclusive with the legacy MMC/SDIO driver.
 */
#include <rtthread.h>
#include <drivers/classes/block.h>
#include <dfs_fs.h>
#include <finsh.h>
#include <stdint.h>
#include <string.h>
#include "drv_sdcard.h"

#if defined(BSP_USING_SDIO) || defined(BSP_USING_T3_PWM0) || \
    (defined(BSP_USING_SPI1) && BSP_SPI1_MAPPING == 3)
#error "TF requires exclusive SD0 and PB0/PB1/PB2 ownership"
#endif
#if RT_TICK_PER_SECOND != 1000
#error "TF timeout conversion expects the board's 1 kHz tick"
#endif
extern uint32_t get_sysclk_nhz(void);
static struct rt_device tf_device;
static struct rt_mutex tf_lock;
static uint8_t staging[512] __attribute__((aligned(32)));
static drv_sdcard_info_t media;
static drv_sdcard_error_t last_error;
static uint32_t source_clock;
static rt_bool_t ready, opened;

static uint32_t time_us(void *unused)
{
    (void)unused;
    /* Real elapsed time, in us with 1 ms resolution; subtraction wraps safely. */
    return (uint32_t)rt_tick_get() * 1000u;
}
static void delay_us(void *unused, uint32_t us)
{
    (void)unused;
    /* SD hardware clocks run independently. Yield during waits, including
     * 1-us poll requests, so the Bluetooth/USB/audio workers can progress. */
    rt_thread_mdelay((us + 999u) / 1000u);
}
static rt_err_t io_error(void)
{
    last_error = drv_sdcard_last_error();
    if (last_error != DRV_SDCARD_ARGUMENT && last_error != DRV_SDCARD_RANGE &&
        last_error != DRV_SDCARD_WRITE_PROTECTED && last_error != DRV_SDCARD_UNSUPPORTED)
        ready = RT_FALSE;
    return last_error == DRV_SDCARD_TIMEOUT ? -RT_ETIMEOUT : -RT_EIO;
}
static rt_err_t check_ready(void)
{
    if (!ready) return -RT_EIO;
    if (source_clock != get_sysclk_nhz())
    {
        drv_sdcard_deinit();
        ready = RT_FALSE;
        last_error = DRV_SDCARD_UNSUPPORTED;
        return -RT_EIO;
    }
    return RT_EOK;
}
/* Caller holds tf_lock; refuse to change media under an open filesystem. */
static rt_err_t probe(void)
{
    drv_sdcard_cfg_t cfg = {0};
    if (opened) return -RT_EBUSY;
    ready = RT_FALSE;
    drv_sdcard_deinit();
    memset(&media, 0, sizeof(media));
    source_clock = get_sysclk_nhz();
    cfg.source_clock_hz = source_clock;
    cfg.transfer_clock_hz = 12000000;
    cfg.time_us = time_us;
    cfg.delay_us = delay_us;
    cfg.dma_buffer = staging;
    /* Identity-mapped, uncached main SRAM; never truncate an arbitrary pointer. */
    uintptr_t address = (uintptr_t)staging;
#ifndef AB32_TF_TEST
    if (address < 0x13800u || address > 0x2f000u - sizeof(staging))
    {
        last_error = DRV_SDCARD_DMA_BUFFER;
        return -RT_EINVAL;
    }
#endif
    cfg.dma_address = (uint32_t)address;
    if (!drv_sdcard_init(&cfg) || !drv_sdcard_get_info(&media)) return io_error();
    /* RV32 rt_off_t is signed. Do not accept media whose LBAs cannot be
     * represented throughout the RTT/DFS interface (limit < 1 TiB). */
    if (!media.block_count || media.block_count > INT32_MAX)
    {
        drv_sdcard_deinit();
        last_error = DRV_SDCARD_UNSUPPORTED;
        return -RT_EINVAL;
    }
    last_error = DRV_SDCARD_OK;
    ready = RT_TRUE;
    return RT_EOK;
}
static rt_err_t tf_open(rt_device_t dev, rt_uint16_t flags)
{
    rt_err_t rc;
    (void)dev; (void)flags;
    rt_mutex_take(&tf_lock, RT_WAITING_FOREVER);
    rc = check_ready();
    if (!rc) opened = RT_TRUE;
    rt_mutex_release(&tf_lock);
    return rc;
}
static rt_err_t tf_close(rt_device_t dev)
{
    (void)dev;
    rt_mutex_take(&tf_lock, RT_WAITING_FOREVER);
    opened = RT_FALSE;
    rt_mutex_release(&tf_lock);
    return RT_EOK;
}
static rt_ssize_t transfer(rt_off_t pos, void *buffer, rt_size_t count, rt_bool_t write)
{
    rt_size_t done = 0;
    rt_err_t rc;
    if (!count) return 0;
    rt_mutex_take(&tf_lock, RT_WAITING_FOREVER);
    rc = check_ready();
    if (rc) goto out;
    if (!buffer || pos < 0 || (uint64_t)pos >= media.block_count ||
        count > media.block_count - (uint64_t)pos || count > SIZE_MAX / 512u)
    {
        rc = -RT_EINVAL;
        goto out;
    }
    for (; done < count; ++done)
    {
        uint8_t *p = (uint8_t *)buffer + done * 512u;
        bool ok = write ? drv_sdcard_write_block((uint32_t)pos + done, p) :
                          drv_sdcard_read_block((uint32_t)pos + done, p);
        if (!ok) { rc = io_error(); break; }
    }
    if (!rc) last_error = DRV_SDCARD_OK;
out:
    rt_mutex_release(&tf_lock);
    if (rc) rt_set_errno(rc);
    /* Preserve the successfully completed prefix on a multi-sector failure. */
    return done ? (rt_ssize_t)done : rc;
}
static rt_ssize_t tf_read(rt_device_t d, rt_off_t p, void *b, rt_size_t n)
{ (void)d; return transfer(p, b, n, RT_FALSE); }
static rt_ssize_t tf_write(rt_device_t d, rt_off_t p, const void *b, rt_size_t n)
{ (void)d; return transfer(p, (void *)b, n, RT_TRUE); }
static rt_err_t tf_control(rt_device_t dev, int cmd, void *arg)
{
    rt_err_t rc;
    (void)dev;
    rt_mutex_take(&tf_lock, RT_WAITING_FOREVER);
    rc = check_ready();
    if (rc) goto out;
    if (cmd == RT_DEVICE_CTRL_BLK_GETGEOME && arg)
    {
        struct rt_device_blk_geometry *g = arg;
        g->sector_count = media.block_count;
        g->bytes_per_sector = 512;
        g->block_size = 512;
    }
    else if (cmd != RT_DEVICE_CTRL_BLK_SYNC)
        rc = -RT_ENOSYS;
    /* No pending software writes: each CMD24 waits for DAT0 and CMD13 ready.
     * SYNC does not claim to detect a card removed since the last command. */
out:
    rt_mutex_release(&tf_lock);
    return rc;
}
static const struct rt_device_ops tf_ops = {
    .open = tf_open, .close = tf_close, .read = tf_read,
    .write = tf_write, .control = tf_control
};
static int tf_register(void)
{
    rt_err_t rc = rt_mutex_init(&tf_lock, "tf", RT_IPC_FLAG_PRIO);
    if (rc) return rc;
    tf_device.type = RT_Device_Class_Block;
    tf_device.ops = &tf_ops;
    return rt_device_register(&tf_device, "tf0", RT_DEVICE_FLAG_RDWR);
}
INIT_DEVICE_EXPORT(tf_register);

static int tf(int argc, char **argv)
{
    int rc = 0;
    if (argc != 2)
    {
        rt_kprintf("tf probe|status|mount|umount|stop\n"
                   "mount: FAT at /, read/write; never automatically formats\n");
        return -RT_EINVAL;
    }
    if (!strcmp(argv[1], "mount"))
    {
        /* Explicit probe first. Never silently reinitialize a failed medium. */
        rc = dfs_mount("tf0", "/", "elm", 0, RT_NULL);
    }
    else if (!strcmp(argv[1], "umount"))
    {
        const char *path = dfs_filesystem_get_mounted_path(&tf_device);
        rc = path ? dfs_unmount(path) : -RT_EINVAL;
    }
    else
    {
        rt_mutex_take(&tf_lock, RT_WAITING_FOREVER);
        if (!strcmp(argv[1], "probe")) rc = probe();
        else if (!strcmp(argv[1], "stop"))
        {
            if (opened) rc = -RT_EBUSY;
            else { drv_sdcard_deinit(); ready = RT_FALSE; }
        }
        else if (strcmp(argv[1], "status")) rc = -RT_EINVAL;
        rt_kprintf("tf0: ready=%d open=%d sectors=%u clock=%u wp=%d error=%d\n",
            ready, opened, (unsigned)media.block_count, media.clock_hz,
            media.write_protected, last_error);
        rt_mutex_release(&tf_lock);
    }
    rt_kprintf("tf %s: %d\n", argv[1], rc);
    return rc;
}
MSH_CMD_EXPORT(tf, Native TF card control);
