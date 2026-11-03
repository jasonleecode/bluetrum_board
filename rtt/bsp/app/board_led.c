/* SPDX-License-Identifier: Apache-2.0
 * V02 red LED: common anode, PE1 active LOW, P1 pins 3-4 connected.
 * Timer5 PWM0 provides the carrier. Main supplies the 10 ms envelope; no IRQ,
 * worker or heap allocation. S2 DOWN toggles breathing without eating keys0.
 */
#include "board.h"
#include "board_led.h"
#include <finsh.h>
#include <string.h>
#ifdef BSP_USING_LED_BREATH
#if !defined(BSP_USING_KEYS) || !defined(BSP_USING_T5_PWM0)
#error "Breathing LED requires S2 keys and Timer5 PWM0"
#endif
#if defined(BSP_USING_I2S) && BSP_I2S_MAPPING == 1
#error "Breathing LED conflicts with I2S PE1"
#endif
#define LED_PIN 14 /* PE1 */
#define LED_PERIOD_NS 250000u /* 4 kHz; fits 16-bit duty even at 120 MHz */
#define LED_HALF_TICKS ((RT_TICK_PER_SECOND * 1500u + 999u) / 1000u)
static struct rt_device_pwm *led_pwm;
static rt_bool_t led_ready, led_active, led_muxed;
static rt_tick_t led_start;
static rt_uint32_t led_level, led_toggles;
static rt_err_t led_error;

static void led_dc(rt_uint8_t level)
{
    /* Preload the GPIO latch before taking the pin away from the timer. */
    rt_pin_write(LED_PIN, level);
    rt_pin_mode(LED_PIN, PIN_MODE_OUTPUT);
    if (led_pwm) rt_pwm_disable(led_pwm, 1);
    led_muxed = RT_FALSE;
}
static rt_err_t led_apply(rt_uint32_t level)
{
    if (!level || level == 1000)
    {
        led_dc(level ? PIN_LOW : PIN_HIGH);
        return RT_EOK;
    }
    /* RT PWM pulse is HIGH time; common-anode LED brightness is LOW time. */
    rt_err_t rc = rt_pwm_set(led_pwm, 1, LED_PERIOD_NS,
                             LED_PERIOD_NS - level * (LED_PERIOD_NS / 1000));
    if (!rc && !led_muxed)
    {
        rc = rt_pwm_enable(led_pwm, 1);
        if (!rc)
        {
            struct gpio_init pin = {0};
            pin.pin = GPIO_PIN_1;
            pin.dir = GPIO_DIR_OUTPUT;
            pin.de = GPIO_DIGITAL;
            pin.alternate = GPIO_AF_MAP_Gx(TMR5MAP_AF, GPIO_AF_G1);
            pin.af_con = GPIO_AFEN | GPIO_AFCON2 | TMR5MAP_AF;
            hal_gpio_init(GPIOE_BASE, &pin);
            led_muxed = RT_TRUE;
        }
    }
    return rc;
}
void board_led_init(void)
{
    led_pwm = (struct rt_device_pwm *)rt_device_find("t5pwm");
    led_dc(PIN_HIGH);
    led_error = rt_pwm_set(led_pwm, 1, LED_PERIOD_NS, LED_PERIOD_NS / 2);
    led_ready = led_error == RT_EOK;
    if (led_error) rt_kprintf("LED PWM init error %d\n", led_error);
}
rt_bool_t board_led_is_active(void) { return led_active; }
rt_err_t board_led_set_active(rt_bool_t active)
{
    if (active && !led_ready) return led_error ? led_error : -RT_EIO;
    if (!!active != !!led_active)
    {
        led_active = !!active;
        led_start = rt_tick_get();
        led_toggles++;
    }
    if (!active)
    {
        led_dc(PIN_HIGH);
        led_level = 0;
    }
    return RT_EOK;
}
void board_led_poll(rt_uint8_t key_downs)
{
    if (!led_ready) return;
    rt_tick_t now = rt_tick_get();
    if (key_downs & 1u)
    {
        board_led_set_active(!led_active);
    }
    rt_uint32_t phase = (rt_tick_t)(now - led_start) % (2 * LED_HALF_TICKS);
    if (phase > LED_HALF_TICKS) phase = 2 * LED_HALF_TICKS - phase;
    rt_uint32_t linear = phase * 1000u / LED_HALF_TICKS;
    rt_uint32_t level = led_active ? linear * linear / 1000u : 0;
    if (level == led_level) return;
    led_error = led_apply(level);
    if (led_error)
    {
        led_dc(PIN_HIGH);
        led_active = RT_FALSE;
        led_ready = RT_FALSE;
        level = 0;
        rt_kprintf("LED PWM error %d\n", led_error);
    }
    led_level = level;
}
static int led(int argc, char **argv)
{
    if (argc != 2 || strcmp(argv[1], "status"))
    {
        rt_kprintf("led status; press S2 to toggle breathing\n");
        return -RT_EINVAL;
    }
    rt_kprintf("LED ready=%u active=%u level=%u/1000 toggles=%u error=%d\n",
        led_ready, led_active, (unsigned)led_level, (unsigned)led_toggles, led_error);
    return RT_EOK;
}
MSH_CMD_EXPORT(led, S2 triggered red breathing LED status);
#endif
