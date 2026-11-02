/* Software-only: real RTT TF adapter + real FatFs/DFS disk glue, RAM media.
 * Formatting is restricted to this host process's allocated fixture. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <rtthread.h>
#define AB32_TF_TEST 1
#include "../../../rtt/components/dfs/dfs_v1/filesystems/elmfat/dfs_elm.c"
#include "../../../rtt/bsp/board/ports/tf/drv_tf.c"

static uint8_t *storage;
static uint64_t sectors;
static int initialized, init_fail, calls, fail_after = -1, protected_card;
static uint32_t clock_hz = 48000000, ticks;
static drv_sdcard_error_t error;
/* Match a board with no RTC set; verify the on-disk FAT fallback date below. */
time_t time(time_t *out) { if (out) *out = 0; return 0; }
uint32_t get_sysclk_nhz(void) { return clock_hz; }
rt_tick_t rt_tick_get(void) { return ticks; }
rt_err_t rt_thread_mdelay(rt_int32_t ms) { ticks += ms; return 0; }
int rt_kprintf(const char *fmt, ...) { (void)fmt; return 0; }
int rt_snprintf(char *s, rt_size_t n, const char *fmt, ...)
{ va_list a; va_start(a, fmt); int r = vsnprintf(s, n, fmt, a); va_end(a); return r; }
void *rt_memset(void *s, int c, rt_ubase_t n) { return memset(s, c, n); }
void rt_set_errno(rt_err_t e) { (void)e; }
rt_err_t rt_mutex_take(rt_mutex_t m, rt_int32_t t) { (void)m; (void)t; return 0; }
rt_err_t rt_mutex_release(rt_mutex_t m) { (void)m; return 0; }
rt_mutex_t rt_mutex_create(const char *n, rt_uint8_t f)
{ (void)n; (void)f; return calloc(1, sizeof(struct rt_mutex)); }
rt_err_t rt_mutex_delete(rt_mutex_t m) { free(m); return 0; }
rt_ssize_t rt_device_read(rt_device_t d, rt_off_t p, void *b, rt_size_t n)
{ return d->ops->read(d, p, b, n); }
rt_ssize_t rt_device_write(rt_device_t d, rt_off_t p, const void *b, rt_size_t n)
{ return d->ops->write(d, p, b, n); }
rt_err_t rt_device_control(rt_device_t d, int c, void *a)
{ return d->ops->control(d, c, a); }
bool drv_sdcard_init(const drv_sdcard_cfg_t *c)
{
    assert(c->source_clock_hz == clock_hz && c->transfer_clock_hz == 12000000);
    assert(!((uintptr_t)c->dma_buffer & 31));
    uint32_t before = c->time_us(NULL);
    c->delay_us(NULL, 1);
    assert((uint32_t)(c->time_us(NULL) - before) == 1000);
    error = init_fail ? DRV_SDCARD_TIMEOUT : DRV_SDCARD_OK;
    initialized = !init_fail;
    return initialized;
}
void drv_sdcard_deinit(void) { initialized = 0; }
drv_sdcard_error_t drv_sdcard_last_error(void) { return error; }
bool drv_sdcard_get_info(drv_sdcard_info_t *i)
{
    if (!initialized) { error = DRV_SDCARD_NOT_READY; return false; }
    *i = (drv_sdcard_info_t){.block_count = sectors, .clock_hz = 12000000,
        .block_addressing = true, .write_protected = protected_card};
    return true;
}
static bool sector_io(uint32_t lba, void *buffer, bool write)
{
    assert(initialized && lba < sectors);
    if (fail_after >= 0 && calls++ == fail_after)
    { error = DRV_SDCARD_DATA_CRC; initialized = 0; return false; }
    if (write && protected_card) { error = DRV_SDCARD_WRITE_PROTECTED; return false; }
    if (write) memcpy(storage + (size_t)lba * 512, buffer, 512);
    else memcpy(buffer, storage + (size_t)lba * 512, 512);
    return true;
}
bool drv_sdcard_read_block(uint32_t lba, uint8_t *b) { return sector_io(lba, b, false); }
bool drv_sdcard_write_block(uint32_t lba, const uint8_t *b) { return sector_io(lba, (void *)b, true); }

static void filesystem_roundtrip(uint32_t nsectors, BYTE format, BYTE expected_type)
{
    FATFS fs;
    FIL file;
    FILINFO info;
    uint8_t work[4096], data[1537], result[sizeof(data)];
    UINT count;
    /* FAT12 superfloppy; FAT16/32 use a partition table as typical cards do. */
    MKFS_PARM options = {.fmt = format | (expected_type == FS_FAT12 ? FM_SFD : 0),
        .n_fat = 1, .au_size = 512};
    sectors = nsectors;
    storage = calloc(nsectors, 512);
    assert(storage && probe() == RT_EOK && tf_open(&tf_device, 0) == RT_EOK);
    disk[0] = &tf_device;
    assert(f_mkfs("0:", &options, work, sizeof(work)) == FR_OK);
    assert(f_mount(&fs, "0:", 1) == FR_OK && fs.fs_type == expected_type);
    for (unsigned i = 0; i < sizeof(data); i++) data[i] = (uint8_t)(i * 17);
    assert(f_open(&file, "0:/TFTEST.BIN", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);
    assert(f_write(&file, data, sizeof(data), &count) == FR_OK && count == sizeof(data));
    assert(f_sync(&file) == FR_OK && f_close(&file) == FR_OK);
    assert(f_stat("0:/TFTEST.BIN", &info) == FR_OK);
    assert(info.fdate == ((40u << 9) | (1u << 5) | 1u));
    assert(f_mount(NULL, "0:", 0) == FR_OK);
    assert(f_mount(&fs, "0:", 1) == FR_OK);
    assert(f_open(&file, "0:/TFTEST.BIN", FA_READ) == FR_OK);
    assert(f_read(&file, result, sizeof(result), &count) == FR_OK && count == sizeof(result));
    assert(!memcmp(data, result, sizeof(data)) && f_close(&file) == FR_OK);
    /* A CRC failure must reach FatFs, invalidate media and fail SYNC/geometry. */
    assert(f_open(&file, "0:/TFTEST.BIN", FA_READ) == FR_OK);
    calls = 0; fail_after = 0;
    assert(f_read(&file, result, sizeof(result), &count) == FR_DISK_ERR);
    assert(!ready && disk_ioctl(0, CTRL_SYNC, NULL) == RES_ERROR);
    DWORD geometry;
    assert(disk_ioctl(0, GET_BLOCK_SIZE, &geometry) == RES_ERROR);
    f_close(&file);
    assert(f_mount(NULL, "0:", 0) == FR_OK);
    tf_close(&tf_device);
    fail_after = -1;
    disk[0] = NULL;
    free(storage);
    printf("PASS: FAT%u RAM disk format/write/sync/remount/read and CRC propagation\n",
        expected_type == FS_FAT32 ? 32 : expected_type == FS_FAT16 ? 16 : 12);
}
int main(void)
{
    uint8_t b[1537];
    sectors = 8192;
    storage = calloc(sectors, 512);
    tf_device.ops = &tf_ops;
    assert(tf_open(&tf_device, 0) == -RT_EIO);
    init_fail = 1;
    assert(probe() == -RT_ETIMEOUT && !ready);
    init_fail = 0;
    assert(probe() == 0 && ready);
    assert(tf_open(&tf_device, 0) == 0 && probe() == -RT_EBUSY);
    assert(tf_read(&tf_device, -1, b, 1) == -RT_EINVAL);
    assert(tf_read(&tf_device, sectors - 1, b, 2) == -RT_EINVAL);
    assert(tf_read(&tf_device, 0, NULL, 1) == -RT_EINVAL);
    assert(tf_read(&tf_device, 0, b, 0) == 0);
    memset(b, 0xa5, sizeof(b));
    assert(tf_write(&tf_device, 10, b + 1, 2) == 2);
    memset(b, 0, sizeof(b));
    assert(tf_read(&tf_device, 10, b + 1, 2) == 2 && b[0] == 0 && b[1024] == 0xa5);
    protected_card = 1;
    assert(tf_write(&tf_device, 10, b, 1) == -RT_EIO && ready);
    protected_card = 0;
    fail_after = 1; calls = 0;
    memset(b, 0x3c, sizeof(b));
    assert(tf_read(&tf_device, 10, b, 3) == 1 && b[512] == 0x3c && !ready);
    assert(tf_read(&tf_device, 0, b, 1) == -RT_EIO);
    tf_close(&tf_device); fail_after = -1;
    assert(probe() == 0);
    calls = 0; fail_after = 1;
    memset(b, 0x3c, sizeof(b));
    assert(tf_write(&tf_device, 10, b, 2) == 1 && !ready);
    assert(storage[10 * 512] == 0x3c && storage[11 * 512] == 0xa5);
    fail_after = -1;
    assert(probe() == 0);
    clock_hz = 24000000;
    assert(tf_read(&tf_device, 0, b, 1) == -RT_EIO && !initialized);
    clock_hz = 48000000;
    sectors = (uint64_t)INT32_MAX + 1;
    assert(probe() == -RT_EINVAL && !ready && !initialized);
    free(storage);
    puts("PASS: TF no-card timeout, bounds, unaligned I/O, partial CRC failure, write protection, clock/capacity guards");
    filesystem_roundtrip(2048, FM_FAT, FS_FAT12);
    filesystem_roundtrip(16384, FM_FAT, FS_FAT16);
    filesystem_roundtrip(131072, FM_FAT32, FS_FAT32);
    assert(disk_ioctl(FF_VOLUMES, CTRL_SYNC, NULL) == RES_NOTRDY);
    assert(disk_read(FF_VOLUMES, b, 0, 1) == RES_NOTRDY);
    return 0;
}
