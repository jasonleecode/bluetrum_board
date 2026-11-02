/* Actual RTC driver, modeled command/data bridge and independently powered state. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "board.h"
volatile rt_uint32_t rtc_mmio[2];
#define AB32_RTC_TEST 1
#include "../../../rtt/bsp/libraries/hal_drivers/drv_rtc.c"

static rt_uint8_t registers[16];
static rt_uint32_t counter, shifted;
static unsigned tear_read, unstable;
static unsigned locked, mutex_owned, irq_nest, no_thread, writes, polls, fail_at, stuck;
static unsigned command, position, direction, busy_cycles, counter_writes, register_failure;
static rt_err_t mutex_failure, take_failure;
static struct rt_thread thread;
static rt_base_t restore_value = 0x10003;
rt_base_t rt_hw_interrupt_disable(void)
{ assert(mutex_owned && !locked); locked = 1; position = 0; return restore_value; }
void rt_hw_interrupt_enable(rt_base_t flags)
{ assert(locked && flags == restore_value && !(RTCCON & RTC_CON_CHIP_SELECT)); locked = 0; }
rt_uint8_t rt_interrupt_get_nest(void) { return irq_nest; }
rt_thread_t rt_thread_self(void) { return no_thread ? NULL : &thread; }
rt_err_t rt_mutex_init(rt_mutex_t m, const char *n, rt_uint8_t f)
{ assert(m == &rtc_lock && !strcmp(n, "rtc") && f == RT_IPC_FLAG_PRIO); return mutex_failure; }
rt_err_t rt_mutex_detach(rt_mutex_t m) { assert(m == &rtc_lock); return 0; }
rt_err_t rt_mutex_take(rt_mutex_t m, rt_int32_t t)
{ assert(m == &rtc_lock && t == RT_WAITING_FOREVER && !locked && !mutex_owned); if (!take_failure) mutex_owned=1; return take_failure; }
rt_err_t rt_mutex_release(rt_mutex_t m) { assert(m == &rtc_lock && mutex_owned && !locked); mutex_owned=0; return 0; }
rt_err_t rt_hw_rtc_register(rt_rtc_dev_t *r, const char *n, rt_uint32_t f, void *d)
{ assert(r == &ab32_rtc_dev && !strcmp(n,"rtc") && f == RT_DEVICE_FLAG_RDWR && !d && r->ops == &ab32_rtc_ops); return register_failure ? -RT_ERROR : 0; }
int rt_kprintf(const char *f, ...) { (void)f; assert(!locked && !mutex_owned); return 0; }
rt_uint32_t rtc_test_control(void)
{
    assert(locked); polls++;
    if (stuck || busy_cycles) { if (busy_cycles) busy_cycles--; return RTCCON | RTC_CON_TRANS_DONE; }
    return RTCCON & ~RTC_CON_TRANS_DONE;
}
void rtc_test_write(rt_uint32_t value)
{
    assert(locked && mutex_owned && (RTCCON & RTC_CON_CHIP_SELECT) && !busy_cycles && !stuck);
    writes++; busy_cycles = 2;
    if (!position++) {
        assert((value & 0x300) == RTC_WR || (value & 0x300) == RTC_RD);
        direction = value & 0x300; command = value & 0xff; shifted = 0;
        assert(command == RTCCNT_CMD || command == RTCCON0_CMD || command == RTCCON2_CMD);
    } else if (direction == RTC_RD) {
        assert(value == 0);
        if (command == RTCCNT_CMD) {
            assert(position <= 5); RTCDAT = (counter >> (8 * (5 - position))) & 0xff;
            if (tear_read && position == 2) { counter++; tear_read = 0; }
            if (unstable && position == 5) counter += 7;
        } else { assert(position == 2); RTCDAT = registers[command]; }
    } else {
        assert(value <= 0xff);
        if (command == RTCCNT_CMD) {
            assert(position <= 5); shifted = (shifted << 8) | value;
            counter = shifted; counter_writes++;
        } else { assert(position == 2); registers[command] = value; }
    }
    if (fail_at == writes) stuck = 1;
}
static void recover(void) { stuck = busy_cycles = fail_at = 0; }
static void reset_model(rt_bool_t valid)
{
    assert(!locked && !mutex_owned);
    memset(registers, 0, sizeof(registers));
    registers[RTCCON0_CMD] = 0x3c | (valid ? 0 : RTC_CON0_PWRUP_FIRST);
    registers[RTCCON2_CMD] = 0x4a;
    counter = 0x65abcdef; rtc_initialized = rtc_uncertain = RT_FALSE;
    RTCCON = RTC_CON_VUSB_RST_ENABLE | RTC_CON_ALM_INTERRUPT | RTC_CON_1S_INTERRUPT |
             RTC_CON_CHIP_SELECT; /* inherited incomplete selection */
    writes = polls = counter_writes = 0; recover();
}
static void check_released(void) { assert(!locked && !mutex_owned && !(RTCCON & RTC_CON_CHIP_SELECT)); }
int main(void)
{
    time_t value = 123;
    assert(RTC_WR == 0x200 && RTC_RD == 0x100 && RTCCNT_CMD == 1 && RTCCON0_CMD == 4 && RTCCON2_CMD == 6);
    assert(ab32_rtc_get_secs(&value) == -RT_EIO && value == 123);
    mutex_failure = -RT_ERROR; assert(rt_hw_rtc_init() == -RT_ERROR && !rtc_ready);
    mutex_failure = 0; register_failure = 1; assert(rt_hw_rtc_init() == -RT_ERROR && !rtc_ready);
    register_failure = 0; assert(rt_hw_rtc_init() == 0 && rtc_ready && !writes);
    assert(!ab32_rtc_ops.get_alarm && !ab32_rtc_ops.set_alarm);
    assert(ab32_rtc_get_secs(NULL) == -RT_EINVAL && ab32_rtc_set_secs(NULL) == -RT_EINVAL);
    value = -1; assert(ab32_rtc_set_secs(&value) == -RT_EINVAL);
    value = (time_t)0x100000000ULL; assert(ab32_rtc_set_secs(&value) == -RT_EINVAL);
    value = 123;
    irq_nest = 1; assert(ab32_rtc_get_secs(&value) == -RT_EBUSY && !writes); irq_nest = 0;
    no_thread = 1; assert(ab32_rtc_init() == -RT_EBUSY && !writes); no_thread = 0;
    take_failure = -RT_ETIMEOUT; assert(ab32_rtc_get_secs(&value) == -RT_ETIMEOUT && !writes); take_failure = 0;

    reset_model(RT_FALSE);
    assert(ab32_rtc_init() == 0 && !counter_writes && counter == 0x65abcdef);
    assert((registers[RTCCON0_CMD] & 0xbc) == 0xbc); /* first flag/power bits preserved */
    assert((registers[RTCCON2_CMD] & 0x7f) == 0x4a);
#ifdef RTC_USING_INTERNAL_CLK
    assert((registers[RTCCON0_CMD] & 0x43) == 0x41 && (registers[RTCCON2_CMD] & 0x80));
#else
    assert((registers[RTCCON0_CMD] & 0x43) == 2 && !(registers[RTCCON2_CMD] & 0x80));
#endif
    assert((RTCCON & (RTC_CON_ALM_INTERRUPT | RTC_CON_1S_INTERRUPT)) == 0);
    assert((RTCCON & RTC_CON_VUSB_RST_ENABLE) && (RTCCON & RTC_CON_BAUD_SELECT) == RTC_CON_BAUD_SELECT);
    unsigned old_writes = writes; assert(ab32_rtc_init() == 0 && writes == old_writes);
    value = 123; assert(ab32_rtc_get_secs(&value) == -RT_EIO && value == 123);
    value = 0x12345678; assert(ab32_rtc_set_secs(&value) == 0 && counter == 0x12345678);
    assert(!(registers[RTCCON0_CMD] & RTC_CON0_PWRUP_FIRST));
    value = 0; assert(ab32_rtc_get_secs(&value) == 0 && value == 0x12345678);
    counter = 0x00ffffff; tear_read = 1;
    assert(ab32_rtc_get_secs(&value) == 0 && value == 0x01000000); /* torn rollover rejected */
    unstable = 1; value = 777;
    assert(ab32_rtc_get_secs(&value) == -RT_EIO && value == 777); unstable = 0;
    value = 0xffffffffULL; assert(ab32_rtc_set_secs(&value) == 0); value=0;
    assert(ab32_rtc_get_secs(&value) == 0 && (rt_uint64_t)value == 0xffffffffULL);
    value=0; assert(ab32_rtc_set_secs(&value) == 0 && counter == 0);
    rtc_initialized = RT_FALSE; counter = 123456; old_writes = counter_writes;
    assert(ab32_rtc_init() == 0 && counter == 123456 && counter_writes == old_writes);
    restore_value = 0; assert(ab32_rtc_get_secs(&value) == 0); restore_value = 0x10003;

    /* Every initialization byte may fail, with CS/IRQ/mutex release and retry. */
    for (unsigned failure = 1; failure <= 8; failure++) {
        reset_model(RT_TRUE); fail_at = failure;
        assert(ab32_rtc_init() == -RT_ETIMEOUT && !rtc_initialized && !counter_writes);
        check_released(); recover(); assert(ab32_rtc_init() == 0);
    }
    reset_model(RT_TRUE); stuck=1;
    assert(ab32_rtc_init() == -RT_ETIMEOUT && !writes && polls == RTC_POLL_LIMIT);
    check_released(); recover(); assert(ab32_rtc_init() == 0);
    /* GET: control read (2 bytes), then two five-byte count transactions. */
    for (unsigned failure = 1; failure <= 12; failure++) {
        fail_at = writes + failure; value=555;
        assert(ab32_rtc_get_secs(&value) == -RT_ETIMEOUT && value == 555);
        check_released(); recover();
    }
    /* SET: preserve an invalid marker if any counter byte or final flag write fails. */
    for (unsigned failure = 1; failure <= 11; failure++) {
        reset_model(RT_TRUE); assert(ab32_rtc_init() == 0);
        fail_at = writes + failure; value=0xabcdef01;
        assert(ab32_rtc_set_secs(&value) == -RT_ETIMEOUT); check_released(); recover();
        if (failure > 2) { value=777; assert(ab32_rtc_get_secs(&value) == -RT_EIO && value==777); }
        value=42; assert(ab32_rtc_set_secs(&value) == 0); assert(ab32_rtc_get_secs(&value) == 0 && value==42);
    }
    char *set[] = {"rtcctl", "set", "4294967295"}; assert(rtcctl(3,set)==0);
    for (const char **p=(const char*[]){"", "-1", "+1", "4294967296", "123x", "999999999999999999999999", NULL}; *p; p++) {
        set[2]=(char*)*p; old_writes=writes; assert(rtcctl(3,set)==-RT_EINVAL && writes==old_writes);
    }
    char *status[] = {"rtcctl", "status"};
    old_writes = counter_writes;
    assert(rtcctl(2, status) == 0 && counter_writes == old_writes);
    check_released();
    puts("PASS: real RTC command IDs, clock selection/preserved bits, unset/retained time, byte order/rollover coherence, 32-bit limits, all-byte timeouts/retry, partial-write invalidation, IRQ/mutex cleanup and parser");
}
