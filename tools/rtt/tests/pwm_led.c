/* Actual driver, PWM API, pin mux, debounce and LED envelope under UBSan. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "board.h"
volatile unsigned long gpio_ports[4][13], gpio_wkup[5], gpio_maps[3];
volatile unsigned long pwm_timers[3][8], pwm_regs[6];
#include "../../../rtt/bsp/libraries/hal_libraries/ab32vg1_hal/source/ab32vg1_hal_gpio.c"
#include "../../../rtt/bsp/board/ab32vg1_hal_msp.c"
#include "../../../rtt/bsp/libraries/hal_drivers/drv_pwm.c"
#include "../../../rtt/components/drivers/misc/rt_drv_pwm.c"
#include "../../../rtt/bsp/app/board_keys.c"
#include "../../../rtt/bsp/app/board_led.c"

static struct rt_thread thread;
static unsigned irq, held, registrations, clocks, fail_lock, no_thread;
static rt_tick_t ticks;
static rt_uint32_t sys_hz = 120000000;
static rt_uint8_t pins = 7, dc_level;
rt_uint32_t get_sysclk_nhz(void) { return sys_hz; }
void hal_rcu_periph_clk_enable(uint32_t periph) { clocks |= 1u << (periph - RCU_TMR3); }
rt_uint8_t rt_interrupt_get_nest(void) { return irq; }
rt_thread_t rt_thread_self(void) { return no_thread ? NULL : &thread; }
rt_err_t rt_mutex_init(rt_mutex_t m, const char *n, rt_uint8_t f) { return 0; }
rt_err_t rt_mutex_detach(rt_mutex_t m) { return 0; }
rt_err_t rt_mutex_take(rt_mutex_t m, rt_int32_t timeout)
{ assert(!irq && !held); if (fail_lock) return -RT_ETIMEOUT; held++; return 0; }
rt_err_t rt_mutex_release(rt_mutex_t m) { assert(held); held--; return 0; }
rt_err_t rt_device_register(rt_device_t d, const char *n, rt_uint16_t flags)
{ registrations++; assert(strlen(n) < RT_NAME_MAX); memcpy(d->parent.name, n, strlen(n)+1); return 0; }
rt_device_t rt_device_find(const char *name)
{ assert(!strcmp(name, "t5pwm")); return &ab32_pwm_obj[T5_PWM_INDEX].pwm_device.parent; }
rt_err_t rt_device_control(rt_device_t d, int cmd, void *arg) { return d->ops->control(d, cmd, arg); }
void *rt_memset(void *s, int c, rt_ubase_t n) { return memset(s,c,n); }
void *rt_memcpy(void *d, const void *s, rt_ubase_t n) { return memcpy(d,s,n); }
int rt_kprintf(const char *f, ...) { return 0; }
rt_tick_t rt_tick_get(void) { return ticks; }
rt_err_t rt_thread_mdelay(rt_int32_t ms) { ticks += ms; return 0; }
void rt_pin_write(rt_base_t pin, rt_ssize_t level) { assert(pin == 14); dc_level = level; }
void rt_pin_mode(rt_base_t pin, rt_uint8_t mode)
{
    if (pin != 14) return;
    assert(mode == PIN_MODE_OUTPUT);
    struct gpio_init p = {.pin=GPIO_PIN_1, .de=GPIO_DIGITAL, .dir=GPIO_DIR_OUTPUT};
    hal_gpio_init(GPIOE_BASE,&p);
}
rt_ssize_t rt_pin_read(rt_base_t pin)
{ return !!(pins & (pin == 22 ? 1 : pin == 21 ? 2 : 4)); }

static void driver_tests(void)
{
    gpio_maps[1] = 0xa5abcdef;
    gpio_maps[2] = 0xa5abcdef;
    unsigned keep = gpio_maps[2] & ~0x000fff00u;
    assert(ab32_pwm_init() == 0 && pwm_ready && clocks == 7 && registrations == 7);
    assert((gpio_maps[2] & ~0x000fff00u) == keep);
    assert(((gpio_maps[2] >> 16) & 15) == GPIO_AF_G1);
    assert((gpio_maps[1] & 0xf000ffffu) == (0xa5abcdefu & 0xf000ffffu));
    assert(((gpio_maps[1] >> LPWM0MAP_AF) & 15) == GPIO_AF_G1);
    assert(((gpio_maps[1] >> LPWM1MAP_AF) & 15) == GPIO_AF_G3);
    assert(((gpio_maps[1] >> LPWM2MAP_AF) & 15) == GPIO_AF_G2);
    struct rt_device_pwm *dev = &ab32_pwm_obj[T5_PWM_INDEX].pwm_device;
    struct rt_pwm_configuration c = {.channel=1};
    assert(rt_pwm_enable(dev,1) == -RT_EINVAL);
    assert(drv_pwm_control(dev,PWM_CMD_SET,NULL) == -RT_EINVAL);
    for (unsigned ch=0; ch<=8; ch++)
        if (ch != 1 && ch != 2 && ch != 4)
            assert(rt_pwm_set(dev,ch,250000,125000) == -RT_EINVAL);
    assert(rt_pwm_set(dev,1,250000,125000) == 0);
    assert(TIM5_BASE[TMRxPR] == 29999 && TIM5_BASE[TMRxDUTY0] == 14999);
    assert(rt_pwm_enable(dev,1) == 0 && TIM5_BASE[TMRxCON] == (BIT(9)|1));
    unsigned long before[8]; memcpy(before,(void*)TIM5_BASE,sizeof(before));
    const unsigned periods[] = {0,1,1000000,0xffffffffu};
    for (unsigned i=0;i<4;i++) assert(rt_pwm_set(dev,1,periods[i],periods[i]/2) == -RT_EINVAL);
    assert(rt_pwm_set(dev,1,250000,250001) == -RT_EINVAL);
    assert(rt_pwm_set(dev,1,250000,0) == -RT_EINVAL);
    assert(rt_pwm_set(dev,1,250000,250000) == -RT_EINVAL);
    assert(!memcmp(before,(void*)TIM5_BASE,sizeof(before)));
    irq=1; assert(rt_pwm_set(dev,1,250000,100000) == -RT_EBUSY); irq=0;
    no_thread=1; assert(rt_pwm_set(dev,1,250000,100000) == -RT_EBUSY); no_thread=0;
    fail_lock=1; assert(rt_pwm_set(dev,1,250000,100000) == -RT_ETIMEOUT); fail_lock=0;
    assert(!held);
    assert(rt_pwm_enable(dev,-1) == -RT_ENOSYS);
    assert(rt_pwm_set(dev,2,250000,100000) == 0);
    assert(rt_pwm_set(dev,1,200000,100000) == -RT_EBUSY);
    assert(rt_pwm_set_pulse(dev,1,50000) == 0 && TIM5_BASE[TMRxDUTY0] == 23999);
    c.channel=2; assert(drv_pwm_control(dev,PWM_CMD_GET,&c) == 0 && c.pulse == 100000);
    assert(rt_pwm_enable(dev,2) == 0);
    assert(rt_pwm_disable(dev,1) == 0 && TIM5_BASE[TMRxCON] == (BIT(10)|1));
    assert(rt_pwm_disable(dev,2) == 0 && TIM5_BASE[TMRxCON] == 0);
    struct rt_device_pwm *lp0 = &ab32_pwm_obj[LPWM0_INDEX].pwm_device;
    struct rt_device_pwm *lp1 = &ab32_pwm_obj[LPWM1_INDEX].pwm_device;
    assert(rt_pwm_set(lp0,1,1000000,250000) == 0);
    unsigned neighbor = pwm_regs[PWMxxDUT] & 0xffff;
    assert(rt_pwm_set(lp1,1,1000000,500000) == 0);
    assert((pwm_regs[PWMxxDUT] & 0xffff) == neighbor && (pwm_regs[PWMxxDUT] >> 16) == 3250);
    assert(rt_pwm_set(lp1,1,2000000,500000) == -RT_EBUSY);
    assert(rt_pwm_set(lp0,1,1000000,0) == 0 && !(pwm_regs[PWMxxDUT] & 0xffff));
    assert(rt_pwm_set(lp0,1,1000000,1000000) == 0 && (pwm_regs[PWMxxDUT] & 0xffff) == 6499);
    puts("PASS: real PWM API, clock/mux, timer start/stop, duty polarity, invalid/overflow no-write, per-channel/shared period, packed LPWM, context errors");
}
static void poll(unsigned now, unsigned levels)
{ ticks=now; pins=levels; board_led_poll(board_keys_poll()); }
static void led_tests(void)
{
    assert(board_keys_init() == 0);
    board_led_init();
    assert(led_ready && !led_active && dc_level == PIN_HIGH && !(GPIOE_BASE[GPIOxFEN]&2));
    poll(0,7); poll(10,6); poll(20,7); poll(30,6); poll(59,6);
    assert(!led_active); poll(60,6); assert(led_active && led_toggles == 1 && led_level == 0);
    poll(810,6); assert(led_level == 250 && led_muxed);
    assert(TIM5_BASE[TMRxDUTY0] == 7499); /* 25% LOW brightness */
    poll(860,6); assert(led_toggles == 1); /* LONG never toggles */
    poll(1560,6); assert(led_level == 1000 && dc_level == PIN_LOW && !led_muxed);
    poll(2310,6); assert(led_level == 250 && led_muxed);
    poll(3060,6); assert(led_level == 0 && dc_level == PIN_HIGH && !led_muxed);
    struct ab32_key_event e[8];
    assert(keys_read(NULL,0,e,sizeof(e)) == 2*sizeof(e[0]));
    assert(e[0].key==1 && e[0].kind==AB32_KEY_DOWN && e[1].kind==AB32_KEY_LONG);
    poll(3070,7); poll(3100,7); poll(3110,5); poll(3140,5);
    assert(led_toggles == 1); /* S3 irrelevant */
    poll(3150,7); poll(3180,7); poll(3190,6); poll(3220,6);
    assert(!led_active && led_toggles==2 && led_level==0 && dc_level==PIN_HIGH);
    /* Tick wrap changes the envelope smoothly; 1500 ticks from start is peak. */
    led_active=1; led_start=(rt_tick_t)-500; ticks=1000; board_led_poll(0);
    assert(led_level==1000);
    /* Runtime PWM failure shuts down visibly rather than leaving a frozen LED. */
    sys_hz=0; ticks=1100; board_led_poll(0);
    assert(!led_ready && !led_active && led_error && dc_level==PIN_HIGH);
    assert(!held);
    puts("PASS: S2 debounce/start/stop, hold once, untouched event queue, S3 ignored, gamma envelope, DC endpoints, tick wrap and fault shutdown");
}
int main(void) { driver_tests(); led_tests(); }
