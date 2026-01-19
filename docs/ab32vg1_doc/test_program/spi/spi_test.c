/*
 * Copyright (c) 2021-2021, Bluetrum Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2021-11-19     greedyhao    the first version
 */

#include <rtthread.h>
#include "driver_spi.h"
#include "board.h"
#include <stdlib.h>

static rt_sem_t sem = RT_NULL;
static uint8_t send_data[100] = {0};
static uint8_t data[100] = {0};
static uint16_t cnt = 0;

// #define SPI_MASTER
#define TEST_WAY 0

RT_SECTION(".irq.spi.str")
static char str[] = ".";

RT_SECTION(".irq.spi")
static void spi_signal_event(uint32_t event)
{
    switch (event) {
        case 0x1:
        #ifndef SPI_MASTER
            GPIOACLR = BIT(2);
        #endif
            rt_kprintf(str);
            rt_sem_release(sem);
            break;
        case 0x2:
            break;
        case 0x4:
            break;
    }
}

int spi_test(void)
{
    blue_driver_version_t version = RT_NULL;
    version = blue_spi1.get_version();
    rt_kprintf("%d %d\n", version->api, version->drv);

#ifdef SPI_MASTER
    rt_kprintf("SPI MASTER\n");
    GPIOAFEN &= ~(BIT(2));
    GPIOADE  |= (BIT(2));
    GPIOADIR |= (BIT(2));
    GPIOAPD  |= (BIT(2));
#else
    rt_kprintf("SPI SLAVE\n");
    GPIOAFEN &= ~(BIT(2));
    GPIOADE  |= (BIT(2));
    GPIOADIR &= ~(BIT(2));
    GPIOACLR |= (BIT(2));
#endif

    sem = rt_sem_create("spi_test", 0, RT_IPC_FLAG_FIFO);
    if (sem == RT_NULL) {
        rt_kprintf("create sem failed!");
    }

    int32_t ret = 0;
    blue_spi1.init(spi_signal_event);
#ifdef SPI_MASTER
    ret |= blue_spi1.control(BLUE_SPI_MODE_MASTER, 40000);
#else
    ret |= blue_spi1.control(BLUE_SPI_MODE_SLAVE, 0);
#endif
    ret |= blue_spi1.control(BLUE_SPI_SET_MAPPING, 4);
    ret |= blue_spi1.control(BLUE_SPI_ENABLE, 0);

    if (ret != 0) {
        rt_kprintf("spi init failed!");
        return -1;
    }

    for (int32_t i = 0; i < 100; i++) {
        send_data[i] = i;
    }

    while (1)
    {
#if TEST_WAY == 0
#ifdef SPI_MASTER
    if (GPIOA & BIT(2)) {
        blue_spi1.send(send_data, 100);
        rt_sem_take(sem, RT_WAITING_FOREVER);
    } else {
        rt_kprintf(".");
        rt_thread_mdelay(500);
    }
#else
    blue_spi1.recv(data, 100);
    GPIOASET = BIT(2);
    rt_sem_take(sem, RT_WAITING_FOREVER);
    for (int i = 0; i < 100; i++)
    {
        rt_kprintf("[%d:%d]", i, data[i]);
    }
    rt_kprintf("\n");

#endif
#elif TEST_WAY == 1
#ifdef SPI_MASTER
    blue_spi1.send_byte(0x55, 1000);
    int32_t recv = blue_spi1.recv_byte(1000);
    if (recv >= 0) {
        rt_kprintf("%x ", recv & 0xff);
    }
#else
    int32_t recv = blue_spi1.recv_byte(-1);
    blue_spi1.send_byte(0x55, 100);
    if (recv >= 0) {
        rt_kprintf("%x ", recv & 0xff);
    }
#endif
#endif
    }
}
MSH_CMD_EXPORT(spi_test, "spi test");
