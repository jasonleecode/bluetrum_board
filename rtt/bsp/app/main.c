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

int main(void)
{
#if !defined(BSP_USING_I2S) || (defined(BSP_I2S_MAPPING) && BSP_I2S_MAPPING == 0)
    uint8_t pin = rt_pin_get("PE.1");

    rt_pin_mode(pin, PIN_MODE_OUTPUT);
    rt_tick_t blink = rt_tick_get();
    rt_uint8_t led = PIN_HIGH;
#endif
    rt_kprintf("Hello, world\n");

    while (1)
    {
#if !defined(BSP_USING_I2S) || (defined(BSP_I2S_MAPPING) && BSP_I2S_MAPPING == 0)
        if ((rt_tick_t)(rt_tick_get() - blink) >= RT_TICK_PER_SECOND / 2)
        {
            blink = rt_tick_get();
            led = !led;
            rt_pin_write(pin, led);
        }
#endif
#ifdef BSP_USING_KEYS
        board_keys_poll();
        rt_thread_mdelay(10);
#else
        rt_thread_mdelay(500);
#endif
    }
}
