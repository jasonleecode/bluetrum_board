/* Actual RTC driver, modeled command/data bridge and independently powered state. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "board.h"
volatile rt_uint32_t rtc_mmio[2];
#define AB32_RTC_TEST 1
#include "../../../rtt/bsp/libraries/hal_drivers/drv_rtc.c"
#ifdef BSP_USING_POWER
/* Exercise each integrated-driver admission branch using state observers. */
#define BSP_USING_BTCTRL 1
#define BSP_USING_AUDIO 1
#define BSP_USING_FMRX 1
#define BSP_USING_USB_CDC 1
#define BSP_USING_TFCARD 1
#define BSP_USING_AUDEC 1
#include "../../../rtt/bsp/libraries/hal_drivers/drv_power.c"
static unsigned power_register_failure, ignore_power_write;
volatile rt_uint32_t power_mmio[5];
static unsigned critical, busy_mask, sleep_calls, terminal_reset;
static jmp_buf terminal;
static void power_transition_tests(void);
rt_bool_t ab32_btctrl_power_busy(void) { assert(critical); return !!(busy_mask & 1); }
rt_bool_t ab32_sound_power_busy(void) { assert(critical); return !!(busy_mask & 2); }
rt_bool_t ab32_fmrx_power_busy(void) { assert(critical); return !!(busy_mask & 4); }
rt_bool_t ab32_usb_power_busy(void) { assert(critical); return !!(busy_mask & 8); }
rt_bool_t ab32_tf_power_busy(void) { assert(critical); return !!(busy_mask & 16); }
rt_bool_t ab32_audec_power_busy(void) { assert(critical); return !!(busy_mask & 32); }
rt_base_t rt_enter_critical(void) { return ++critical; }
void rt_exit_critical(void) { assert(critical); --critical; }
rt_uint16_t rt_critical_level(void) { return critical; }
static rt_tick_t model_tick;
static unsigned frozen_rtc;
static unsigned spurious_wake, fail_wake_read, start_busy_during_alignment;
rt_tick_t rt_tick_get(void) { return model_tick; }
void rt_tick_set(rt_tick_t tick) { model_tick=tick; }
rt_err_t rt_thread_mdelay(rt_int32_t ms);
static void power_tests(void);
rt_err_t rt_device_register(rt_device_t d, const char *n, rt_uint16_t flags)
{
    assert(d == &power_device && !strcmp(n,"power0") && flags == RT_DEVICE_FLAG_RDWR);
    return power_register_failure ? -RT_ERROR : 0;
}
void *rt_memcpy(void *d, const void *s, rt_ubase_t size) { return memcpy(d,s,size); }
#endif

static rt_uint8_t registers[16];
static rt_uint32_t counter, shifted;
static unsigned tear_read, unstable;
static unsigned locked, mutex_owned, irq_nest, no_thread, writes, polls, fail_at, stuck;
static unsigned command, position, direction, busy_cycles, counter_writes, register_failure;
static unsigned transient_fault, fault_polls, change_wk_at;
static rt_err_t mutex_failure, take_failure;
static struct rt_thread thread;
static rt_base_t restore_value = 0x10003;
rt_base_t rt_hw_interrupt_disable(void)
{ assert(mutex_owned); rt_base_t prior=locked ? 0 : restore_value; ++locked; position=0; return prior; }
void rt_hw_interrupt_enable(rt_base_t flags)
{ assert(locked && !(RTCCON & RTC_CON_CHIP_SELECT)); --locked; assert(flags == (locked ? 0 : restore_value)); }
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
    if (fault_polls) { --fault_polls; return RTCCON | RTC_CON_TRANS_DONE; }
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
        assert(command == RTCCNT_CMD || command == RTCCON0_CMD || command == RTCCON2_CMD
#ifdef BSP_USING_POWER
               || command == RTCCON1_CMD || command == RTCCON3_CMD || command == RTCCON5_CMD
               || command == RTCCON7_CMD || command == RTCCON8_CMD || command == RTCCON9_CMD
#endif
        );
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
        } else {
            assert(position == 2);
#ifdef BSP_USING_POWER
            if (!ignore_power_write)
#endif
                registers[command] = value;
        }
    }
    if (fail_at == writes) {
        if (transient_fault) { fault_polls=RTC_POLL_LIMIT;busy_cycles=0; }
        else stuck = 1;
    }
    if (change_wk_at == writes) RTCCON &= ~RTC_CON_WK_PIN_STATE;
}
static void recover(void) { stuck = busy_cycles = fail_at = fault_polls = transient_fault = change_wk_at = 0; }
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
#ifdef BSP_USING_POWER
    power_tests();
#endif
    puts("PASS: real RTC command IDs, clock selection/preserved bits, unset/retained time, byte order/rollover coherence, 32-bit limits, all-byte timeouts/retry, partial-write invalidation, IRQ/mutex cleanup and parser");
}

#ifdef BSP_USING_POWER
/* Verify private write restrictions inside the same serialized transaction. */
static rt_err_t forbidden(void *arg)
{
    (void)arg;
    unsigned before=writes;
    assert(ab32_irtc_update8_locked(RTCCON0_CMD,0xff,0) == -RT_EINVAL);
    assert(ab32_irtc_update8_locked(RTCCON3_CMD,7,0) == -RT_EINVAL);
    assert(ab32_irtc_update8_locked(RTCCON8_CMD,0xff,0xff) == -RT_EINVAL);
    assert(ab32_irtc_update8_locked(RTCCON9_CMD,15,0) == -RT_EINVAL);
    assert(ab32_irtc_update8_locked(RTCCON7_CMD,0x10,0) == -RT_EINVAL);
    assert(ab32_irtc_update8_locked(RTCCON3_CMD,0x10,0x20) == -RT_EINVAL);
    assert(writes==before);
    return 0;
}
static void power_tests(void)
{
    struct ab32_power_status s, untouched;
    struct ab32_power_wake w={AB32_POWER_WAKE_PWKEY,AB32_POWER_WAKE_PWKEY};
    struct ab32_power_input input={1,1};
    rt_uint32_t ms=32;
    reset_model(RT_FALSE);
    PICCON=1;
    assert(power_control(NULL,AB32_POWER_GET_STATUS,&s)==-RT_EIO);
    power_register_failure=1; assert(ab32_power_init()==-RT_ERROR && !power_ready);
    power_register_failure=0; assert(ab32_power_init()==0 && power_ready && !writes);
    for (unsigned n=0;n<16;n++) registers[n]=(rt_uint8_t)(0xc0+n);
    registers[RTCCON0_CMD] |= RTC_CON0_PWRUP_FIRST;
    registers[RTCCON1_CMD]=0x9d;
    registers[RTCCON3_CMD]=0xff;
    registers[RTCCON7_CMD]=0xac; /* filter on, 8 ms, preserve high bits */
    registers[RTCCON8_CMD]=0x1f;
    rt_uint8_t original[16]; memcpy(original,registers,sizeof(original));
    RTCCON |= RTC_CON_WK_PIN_STATE | RTC_CON_VUSB_OLINE;
    rt_uint32_t original_bridge=RTCCON;
    assert(power_control(NULL,AB32_POWER_GET_STATUS,&s)==0);
    assert(s.pwkey_level && s.vusb_online && s.pwkey_input && s.pwkey_pullup);
    assert(s.wake_enabled==0xf0 && s.wake_pending==0x1f && s.power_rails==7);
    assert(s.filter_ms==8 && s.filter_enabled && s.sleep_supported && s.poweroff_supported);
    assert(!memcmp(original,registers,sizeof(original)) && !counter_writes && !rtc_initialized);
    assert((RTCCON & ~(RTC_CON_CHIP_SELECT|RTC_CON_BAUD_SELECT)) ==
           (original_bridge & ~(RTC_CON_CHIP_SELECT|RTC_CON_BAUD_SELECT)));
    assert((RTCCON & (RTC_CON_ALM_INTERRUPT|RTC_CON_1S_INTERRUPT)) ==
           (RTC_CON_ALM_INTERRUPT|RTC_CON_1S_INTERRUPT));
    assert(ab32_irtc_transaction(forbidden,NULL)==0);
    unsigned before=writes;
    assert(power_control(NULL,AB32_POWER_GET_STATUS,NULL)==-RT_EINVAL);
    assert(power_control(NULL,9999,NULL)==-RT_ENOSYS);
    PICCON=1;
    assert(power_control(NULL,AB32_POWER_SLEEP,NULL)==-RT_EBUSY); /* no wake tick */
    assert(power_control(NULL,AB32_POWER_OFF,NULL)==-RT_EINVAL);
    w.mask=7; w.enabled=0; assert(power_control(NULL,AB32_POWER_SET_WAKE,&w)==-RT_EINVAL);
    w.mask=0x10;w.enabled=0x20;assert(power_control(NULL,AB32_POWER_SET_WAKE,&w)==-RT_EINVAL);
    input.enabled=2;assert(power_control(NULL,AB32_POWER_SET_INPUT,&input)==-RT_EINVAL);
    ms=9;assert(power_control(NULL,AB32_POWER_SET_FILTER,&ms)==-RT_EINVAL);
    assert(writes==before);
    irq_nest=1;assert(power_control(NULL,AB32_POWER_GET_STATUS,&s)==-RT_EBUSY);irq_nest=0;
    no_thread=1;assert(power_control(NULL,AB32_POWER_GET_STATUS,&s)==-RT_EBUSY);no_thread=0;
    take_failure=-RT_ETIMEOUT;assert(power_control(NULL,AB32_POWER_GET_STATUS,&s)==-RT_ETIMEOUT);take_failure=0;
    assert(writes==before);
    w=(struct ab32_power_wake){0x10,0};
    assert(power_control(NULL,AB32_POWER_SET_WAKE,&w)==0 && registers[RTCCON3_CMD]==0xef);
    w=(struct ab32_power_wake){0x20,0};
    assert(power_control(NULL,AB32_POWER_SET_WAKE,&w)==0 && registers[RTCCON3_CMD]==0xcf);
    w=(struct ab32_power_wake){0x30,0x30};
    assert(power_control(NULL,AB32_POWER_SET_WAKE,&w)==0 && registers[RTCCON3_CMD]==0xff);
    registers[RTCCON1_CMD]=0xef; input=(struct ab32_power_input){1,1};
    assert(power_control(NULL,AB32_POWER_SET_INPUT,&input)==0 && registers[RTCCON1_CMD]==0xfd);
    input=(struct ab32_power_input){0,0};
    assert(power_control(NULL,AB32_POWER_SET_INPUT,&input)==0 && registers[RTCCON1_CMD]==0xec);
    const unsigned filters[]={0,8,32,128,512};
    for (unsigned i=0;i<5;i++) {
        ms=filters[i];assert(power_control(NULL,AB32_POWER_SET_FILTER,&ms)==0);
        assert(power_control(NULL,AB32_POWER_GET_STATUS,&s)==0 && s.filter_ms==ms);
        assert((registers[RTCCON7_CMD]&0xf8)==(original[RTCCON7_CMD]&0xf8));
    }
    for (unsigned n=0;n<16;n++)
        if(n!=RTCCON1_CMD && n!=RTCCON3_CMD && n!=RTCCON7_CMD) assert(registers[n]==original[n]);
    assert(!rtc_initialized && !counter_writes);
    /* All 16 status command/read bytes: no partial snapshot escapes. */
    memset(&untouched,0xa5,sizeof(untouched));
    for(unsigned failure=1;failure<=16;failure++) {
        s=untouched;fail_at=writes+failure;
        assert(power_control(NULL,AB32_POWER_GET_STATUS,&s)==-RT_ETIMEOUT);
        assert(!memcmp(&s,&untouched,sizeof(s)));check_released();recover();
        assert(power_control(NULL,AB32_POWER_GET_STATUS,&s)==0);
    }
    /* read(2), write(2), read-back(2); a failed write may already be applied. */
    for(unsigned failure=1;failure<=6;failure++) {
        registers[RTCCON3_CMD]=0xcf; w=(struct ab32_power_wake){0x10,0x10};
        fail_at=writes+failure;
        assert(power_control(NULL,AB32_POWER_SET_WAKE,&w)==-RT_ETIMEOUT);
        check_released();recover();
        assert((registers[RTCCON3_CMD]&~0x10u)==0xcf);
        assert(power_control(NULL,AB32_POWER_SET_WAKE,&w)==0 && registers[RTCCON3_CMD]==0xdf);
    }
    ignore_power_write=1; w=(struct ab32_power_wake){0x10,0};
    assert(power_control(NULL,AB32_POWER_SET_WAKE,&w)==-RT_EIO);ignore_power_write=0;
    /* RTC access after PM still initializes correctly and uses the same lock. */
    time_t time=42;assert(ab32_rtc_set_secs(&time)==0);time=0;
    assert(ab32_rtc_get_secs(&time)==0 && time==42);
    assert(power_control(NULL,AB32_POWER_GET_STATUS,&s)==0 && counter==42);
    char *args[]={"pmctl","status","unused","unused"};assert(pmctl(2,args)==0);
    args[1]="wake";args[2]="pwkey";args[3]="off";assert(pmctl(4,args)==0);
    args[2]="vusb";args[3]="on";assert(pmctl(4,args)==0);
    args[1]="filter";args[2]="128";assert(pmctl(3,args)==0);
    args[1]="input";args[2]="on";args[3]="on";assert(pmctl(4,args)==0);
    args[1]="sleep";before=writes;assert(pmctl(2,args)==-RT_EBUSY && writes==before);
    args[1]="off";args[2]="invalid";assert(pmctl(3,args)==-RT_EINVAL && writes==before);
    args[1]="wake";args[2]="rtc";args[3]="on";assert(pmctl(4,args)==-RT_EINVAL && writes==before);
    args[1]="filter";args[2]="32x";assert(pmctl(3,args)==-RT_EINVAL && writes==before);
    args[1]="input";args[2]="1";assert(pmctl(4,args)==-RT_EINVAL && writes==before);
    check_released();
    power_transition_tests();
    puts("PASS: PM registration no-write, shared RTC locking, preserved clock/time/rails/pending/reset bits, wake/input/filter, all-byte faults/readback, atomic snapshot and thread context");
}

void power_test_sleep(void)
{
    assert(critical == 1 && locked == 1 && mutex_owned);
    assert((PICEN & 8) && TMR0CON == 0x80 && TMR0PR);
    assert((RTCCON & 0x1f8)==RTC_CON_1S_WK_ENABLE);
    assert(!LPMCON);
    ++sleep_calls;
    if (spurious_wake) { --spurious_wake; LPMCON=1; return; }
    ++counter;
    LPMCON=1; RTCCON |= RTC_CON_1S_PEND;
    if (fail_wake_read) { fail_at=writes+1;transient_fault=1; }
}
void power_test_rtc_pending_clear(void) { RTCCON &= ~RTC_CON_1S_PEND; }
rt_err_t rt_thread_mdelay(rt_int32_t ms)
{
    assert(!critical && !locked && (ms==1 || ms==20));
    for(rt_int32_t i=0;i<ms;i++) {
        ++model_tick;
        if(!frozen_rtc && model_tick % RT_TICK_PER_SECOND == 0) ++counter;
    }
    if(start_busy_during_alignment && ms==1) busy_mask=1;
    return 0;
}
void power_test_terminal(rt_bool_t reset)
{
    assert(locked == 1 && mutex_owned && critical == 1);
    assert((RTCCON & 0x1e0) == RTC_CON_WK_RST_ENABLE);
    terminal_reset = reset;
    longjmp(terminal,1);
}
static void setup_off(void)
{
    reset_model(RT_TRUE);
    PICCON=1; PICEN=8; TMR0CON=0x81; TMR0PR=47999; LPMCON=0;
    registers[RTCCON0_CMD]=0x7d; /* RTC clock/VDD, LVD enabled */
    registers[RTCCON1_CMD]=0x15; /* digital input, pull-up, analog disabled */
    registers[RTCCON3_CMD]=0xff; /* all old wake sources, all rails */
    registers[RTCCON5_CMD]=0xbe;
    registers[RTCCON7_CMD]=0x1d;
    registers[RTCCON8_CMD]=0x1f;
    registers[RTCCON9_CMD]=0xa;
    RTCCON=(RTCCON & ~RTC_CON_CHIP_SELECT) | RTC_CON_WK_PIN_STATE;
}
static void power_transition_tests(void)
{
    struct ab32_power_off off={AB32_POWER_WAKE_PWKEY};
    struct ab32_power_status s;
    setup_off();
    unsigned before=writes;
    assert(power_capture_boot()==0 && boot_valid && boot_pending==0x1f && !boot_first_power);
    assert(!counter_writes && !rtc_initialized);
    registers[RTCCON8_CMD]=0; /* live changes must not alter the boot snapshot */
    assert(power_control(NULL,AB32_POWER_GET_STATUS,&s)==0 && s.boot_pending==0x1f && !s.wake_pending);
    before=writes;
    rt_tick_t tick_before=model_tick;
    assert(power_control(NULL,AB32_POWER_SLEEP,NULL)==0 && sleep_calls==1 && writes>before);
    assert(model_tick-tick_before>=RT_TICK_PER_SECOND && !LPMCON);
    assert(!critical && !locked && !mutex_owned && PICCON==1 && PICEN==8 && TMR0CON==0x81 && TMR0PR==47999);
    unsigned entries_before=sleep_calls;
    frozen_rtc=1;
    assert(power_control(NULL,AB32_POWER_SLEEP,NULL)==-RT_ETIMEOUT);
    assert(sleep_calls==entries_before && !critical);check_released();frozen_rtc=0;
    start_busy_during_alignment=1;
    assert(power_control(NULL,AB32_POWER_SLEEP,NULL)==-RT_EBUSY);
    assert(sleep_calls==entries_before && !critical);check_released();
    start_busy_during_alignment=busy_mask=0;
    spurious_wake=1;
    assert(power_control(NULL,AB32_POWER_SLEEP,NULL)==0 && sleep_entries==2 && !LPMCON);
    assert(TMR0CON==0x81);check_released();
    rt_uint32_t wake_policy=RTCCON & 0x1f8;
    fail_wake_read=1;
    assert(power_control(NULL,AB32_POWER_SLEEP,NULL)==-RT_ETIMEOUT);
    assert(TMR0CON==0x81 && !LPMCON && (RTCCON&0x1f8)==wake_policy && !critical);check_released();
    fail_wake_read=0;recover();
    model_tick=0xfffffff0u;
    tick_before=model_tick;
    assert(power_control(NULL,AB32_POWER_SLEEP,NULL)==0);
    assert((rt_tick_t)(model_tick-tick_before)==RT_TICK_PER_SECOND+16u);
    for (unsigned b=1;b<=32;b<<=1) {
        busy_mask=b;before=writes;
        assert(power_control(NULL,AB32_POWER_SLEEP,NULL)==-RT_EBUSY && writes==before);
        /* Bridge selection is non-destructive; no IRTC byte writes on busy. */
        assert(power_control(NULL,AB32_POWER_OFF,&off)==-RT_EBUSY && writes==before);
        assert(power_control(NULL,AB32_POWER_GET_STATUS,&s)==0 && s.blockers==b);
    }
    busy_mask=0;
    for(unsigned c=0;c<5;c++) {
        rt_uint32_t saved=power_mmio[c];power_mmio[c]=c==4 ? 4 : 0;
        assert(power_control(NULL,AB32_POWER_SLEEP,NULL)==-RT_EBUSY);
        power_mmio[c]=saved;
    }
    before=writes;
    critical=1;assert(power_control(NULL,AB32_POWER_SLEEP,NULL)==-RT_EBUSY);
    assert(power_control(NULL,AB32_POWER_OFF,&off)==-RT_EBUSY);critical=0;
    irq_nest=1;assert(power_control(NULL,AB32_POWER_OFF,&off)==-RT_EBUSY);irq_nest=0;
    no_thread=1;assert(power_control(NULL,AB32_POWER_SLEEP,NULL)==-RT_EBUSY);no_thread=0;
    assert(power_control(NULL,AB32_POWER_SLEEP,&off)==-RT_EINVAL);
    off.wake_sources=0;assert(power_control(NULL,AB32_POWER_OFF,&off)==-RT_EINVAL);
    off.wake_sources=0x80;assert(power_control(NULL,AB32_POWER_OFF,&off)==-RT_EINVAL);
    assert(writes==before);
    off.wake_sources=0x10;
    setup_off();registers[RTCCON1_CMD]|=0x20;
    assert(power_control(NULL,AB32_POWER_OFF,&off)==-RT_EINVAL);check_released();assert(!critical);
    setup_off();RTCCON &= ~RTC_CON_WK_PIN_STATE;
    assert(power_control(NULL,AB32_POWER_OFF,&off)==-RT_EBUSY);check_released();assert(!critical);
    setup_off();RTCCON |= RTC_CON_VUSB_OLINE;off.wake_sources=0x20;
    assert(power_control(NULL,AB32_POWER_OFF,&off)==-RT_EBUSY);check_released();assert(!critical);
    setup_off();registers[RTCCON3_CMD]&=~4u;
    assert(power_control(NULL,AB32_POWER_OFF,&off)==-RT_EIO);check_released();
    setup_off();registers[RTCCON0_CMD]&=~RTC_CON0_VDD_ENABLE;
    assert(power_control(NULL,AB32_POWER_OFF,&off)==-RT_EIO);check_released();

    /* First 8 bytes are reads, then three checked preparations of 6 bytes
     * each. An indefinitely stuck bridge cannot roll back: report EIO, never
     * reach terminal code or write the rails. */
    off.wake_sources=0x10;
    for(unsigned failure=1;failure<=26;failure++) {
        setup_off();fail_at=failure;
        rt_err_t rc=power_control(NULL,AB32_POWER_OFF,&off);
        assert(rc==(failure<=8 ? -RT_ETIMEOUT : -RT_EIO));
        assert((registers[RTCCON3_CMD]&7)==7 && !counter_writes && !LPMCON);
        check_released();assert(!critical);recover();
    }
    setup_off();ignore_power_write=1;
    assert(power_control(NULL,AB32_POWER_OFF,&off)==-RT_EIO);
    ignore_power_write=0;assert(registers[RTCCON3_CMD]==0xff && registers[RTCCON0_CMD]==0x7d && registers[RTCCON5_CMD]==0xbe);
    check_released();assert(!critical);
    for(unsigned failure=9;failure<=26;failure++) {
        setup_off();fail_at=failure;transient_fault=1;
        rt_uint32_t bridge=RTCCON;
        assert(power_control(NULL,AB32_POWER_OFF,&off)==-RT_ETIMEOUT);
        assert(registers[RTCCON3_CMD]==0xff && registers[RTCCON0_CMD]==0x7d && registers[RTCCON5_CMD]==0xbe);
        assert((RTCCON&0x1e0)==(bridge&0x1e0));check_released();assert(!critical);recover();
    }
    setup_off();change_wk_at=26;
    assert(power_control(NULL,AB32_POWER_OFF,&off)==-RT_EBUSY);
    assert(registers[RTCCON3_CMD]==0xff && registers[RTCCON0_CMD]==0x7d && registers[RTCCON5_CMD]==0xbe);
    check_released();assert(!critical);recover();

    /* Successful commits for all sources; longjmp models loss of CPU power.
     * Final command/data timeout must reset, never return to runnable RTT. */
    for(unsigned source=0x10;source<=0x30;source+=0x10) {
        for(unsigned failure=0;failure<=2;failure++) {
            setup_off();off.wake_sources=source;
            if(failure) fail_at=26+failure;
            terminal_reset=99;
            if(!setjmp(terminal)) { power_control(NULL,AB32_POWER_OFF,&off); assert(!"OFF returned after commit"); }
            assert(terminal_reset==!!failure);
            if(!failure) assert(registers[RTCCON3_CMD]==(source|8));
            assert(registers[RTCCON0_CMD]==0x79 && registers[RTCCON1_CMD]==0x15 && registers[RTCCON5_CMD]==0xbd);
            assert(registers[RTCCON7_CMD]==0x1d && registers[RTCCON9_CMD]==0xa && !counter_writes);
            locked=mutex_owned=critical=0; /* simulated reset, no normal cleanup */
            recover();
        }
    }
    setup_off();
    char *args[]={"pmctl","sleep","unused"};assert(pmctl(2,args)==0);
    args[1]="off";busy_mask=1;assert(pmctl(2,args)==-RT_EBUSY);
    args[2]="vusb";assert(pmctl(3,args)==-RT_EBUSY);
    args[2]="both";assert(pmctl(3,args)==-RT_EBUSY);
    args[2]="pwkey";assert(pmctl(3,args)==-RT_EBUSY);
    busy_mask=0;
    puts("PASS: retained-clock sleep/wake guards, all integrated blockers, raw boot snapshot, off source/level validation, every preparation/commit byte failure, rollback and terminal reset-on-failure");
}
#endif
