/*
 * Copyright (c) 2020-2021, Bluetrum Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2020/12/10     greedyhao    The first version
 */

/**
 * Notice!
 * All functions or data that are called during an interrupt need to be in RAM.
 * You can do it the way exception_isr() does.
 */

#include <rtthread.h>
#include "board.h"
#include "board_keys.h"
#include "board_led.h"
#ifdef BSP_USING_IRRX
#include "drv_irrx.h"
#endif

#if !defined(BSP_USING_LED_BREATH) && !defined(BSP_USING_T5_PWM0) && \
    (!defined(BSP_USING_I2S) || (defined(BSP_I2S_MAPPING) && BSP_I2S_MAPPING == 0))
#define LEGACY_LED_BLINK
#endif

int main(void)
{
#ifdef BSP_USING_LED_BREATH
    board_led_init();
#endif
#ifdef LEGACY_LED_BLINK
    uint8_t pin = rt_pin_get("PE.1");

    rt_pin_mode(pin, PIN_MODE_OUTPUT);
    rt_tick_t blink = rt_tick_get();
    rt_uint8_t led = PIN_HIGH;
#endif
    rt_kprintf("Hello, world\n");

    while (1)
    {
#ifdef LEGACY_LED_BLINK
        if ((rt_tick_t)(rt_tick_get() - blink) >= RT_TICK_PER_SECOND / 2)
        {
            blink = rt_tick_get();
            led = !led;
            rt_pin_write(pin, led);
        }
#endif
#ifdef BSP_USING_KEYS
#ifdef BSP_USING_LED_BREATH
        board_led_poll(board_keys_poll());
#else
        board_keys_poll();
#endif
#endif
#ifdef BSP_USING_IRRX
        ab32_irrx_poll();
#endif
#if defined(BSP_USING_KEYS) || defined(BSP_USING_IRRX)
        rt_thread_mdelay(10);
#else
        rt_thread_mdelay(500);
#endif
    }
}
