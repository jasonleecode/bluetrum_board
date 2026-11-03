/* Exercise the real date shell command with an unset/failing RTC device. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <rtthread.h>
#include <drivers/dev_rtc.h>
static int date_gettimeofday(struct timeval *tv, void *tz);
int32_t rt_tz_get(void);
#define gettimeofday date_gettimeofday
#include "../../../rtt/components/drivers/rtc/dev_rtc.c"
#undef gettimeofday

static struct rt_device device;
static int present = 1, valid, writes, reads, fail_write, fail_readback;
static time_t seconds;
static char output[2048];
static size_t used;
int32_t rt_tz_get(void) { return 8 * 3600; }

rt_device_t rt_device_find(const char *name)
{ assert(!strcmp(name, "rtc")); return present ? &device : NULL; }
rt_err_t rt_device_control(rt_device_t dev, int cmd, void *arg)
{
    assert(dev == &device);
    if (cmd == RT_DEVICE_CTRL_RTC_GET_TIME)
    {
        reads++;
        if (!valid || (writes && fail_readback)) return -RT_EIO;
        *(time_t *)arg = seconds;
        return RT_EOK;
    }
    assert(cmd == RT_DEVICE_CTRL_RTC_SET_TIME);
    writes++;
    if (fail_write) return -RT_ETIMEOUT;
    seconds = *(time_t *)arg;
    valid = 1;
    return RT_EOK;
}
static int date_gettimeofday(struct timeval *tv, void *tz)
{
    assert(!tz);
    tv->tv_usec = 0;
    return get_timestamp(&tv->tv_sec) ? -1 : 0;
}
int rt_kprintf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(output + used, sizeof(output) - used, fmt, args);
    va_end(args);
    assert(n >= 0 && (size_t)n < sizeof(output) - used);
    used += n;
    return n;
}
static void reset(void)
{
    _rtc_device = NULL;
    present = 1; valid = writes = reads = fail_write = fail_readback = 0;
    used = 0; output[0] = 0; seconds = 0;
}
int main(void)
{
    char *set[] = {"date", "2026", "11", "03", "12", "34", "56"};
    char *get[] = {"date"};
    setenv("TZ", "UTC-8", 1); tzset();
    reset();
    date(1, get);
    assert(strstr(output, "Read RTC failed") && !strstr(output, "1970") && !writes);
    reset();
    date(7, set);
    assert(valid && writes == 1 && reads == 2 && seconds == 1793680496);
    assert(strstr(output, "old: unavailable") && strstr(output, "now:") && !strstr(output, "set date failed"));
    used = 0; output[0] = 0;
    date(7, set);
    assert(writes == 2 && strstr(output, "old:") && !strstr(output, "unavailable"));
    used = 0; output[0] = 0;
    date(1, get);
    assert(strstr(output, "local time:") && strstr(output, "2026"));
    reset(); fail_write = 1;
    date(7, set);
    assert(writes == 1 && !valid && strstr(output, "set date failed") && !strstr(output, "now:"));
    reset(); fail_readback = 1;
    date(7, set);
    assert(writes == 1 && valid && strstr(output, "readback failed") && !strstr(output, "now:"));
    reset(); present = 0;
    date(7, set);
    assert(!writes && strstr(output, "set date failed"));
    puts("PASS: date first setting, existing time, UTC+8 conversion, read/write/readback errors, absent RTC");
}
