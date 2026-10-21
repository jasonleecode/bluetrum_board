/*
 * Copyright (c) 2006-2021, Bluetrum Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2021-11-16     greedyhao    first version
 */

#include "board.h"
#include "drv_flash.h"

#ifdef BSP_USING_ON_CHIP_FLASH

#if defined(RT_USING_FAL)
#include "fal.h"
#endif

//#define DRV_DEBUG
#define LOG_TAG                "drv.flash"
#include <drv_log.h>

#if defined(RT_USING_FAL)

#define AB32_FLASH_START_ADDRESS    0x00000000
#define AB32_FLASH_SIZE             (1024 * 1024)
#define AB32_FLASH_PAGE_SIZE        (0x1000)

static int fal_flash_read(long offset, rt_uint8_t *buf, size_t size);
static int fal_flash_write(long offset, const rt_uint8_t *buf, size_t size);
static int fal_flash_erase(long offset, size_t size);

const struct fal_flash_dev ab32_onchip_flash =
{
    "onchip_flash",
    AB32_FLASH_START_ADDRESS,
    AB32_FLASH_SIZE,
    AB32_FLASH_PAGE_SIZE,
    {NULL, fal_flash_read, fal_flash_write, fal_flash_erase},
    256 * 8
};

static int flash_range_valid(long offset, size_t size)
{
    return offset >= 0 && (size_t)offset <= AB32_FLASH_SIZE &&
           size <= AB32_FLASH_SIZE - (size_t)offset;
}

static int fal_flash_read(long offset, rt_uint8_t *buf, size_t size)
{
    size_t done = 0;
    if (!flash_range_valid(offset, size) || (!buf && size))
        return -RT_EINVAL;
    while (done < size)
    {
        size_t chunk = size - done;
        if (chunk > 256) chunk = 256;
        /* The SDK returns a CRC16, not a byte count (libhal disassembly). */
        os_spiflash_read(buf + done, offset + done, (uint16_t)chunk);
        done += chunk;
    }
    return (int)done;
}

static int fal_flash_write(long offset, const rt_uint8_t *buf, size_t size)
{
    size_t done;
    if (!flash_range_valid(offset, size) || (!buf && size) ||
        (offset % 256) || (size % 256))
        return -RT_EINVAL;
    /* The original function issues one page-program command per call. */
    for (done = 0; done < size; done += 256)
        os_spiflash_program(buf + done, offset + done, 256);
    return (int)size;
}

static int fal_flash_erase(long offset, size_t size)
{
    size_t done;
    if (!flash_range_valid(offset, size) || (offset % 4096) || (size % 4096))
        return -RT_EINVAL;
    for (done = 0; done < size; done += 4096)
        os_spiflash_erase(offset + done);
    return (int)size;
}

int fal_ops_test(void)
{
    int result;
    const struct fal_partition *part_dev = fal_partition_find("param");
    uint8_t *data = rt_malloc(256);
    int i;
    int size = 256;
    int addr = 0;

    if (!data || !part_dev)
    {
        if (data) rt_free(data);
        return -RT_ERROR;
    }
    for (int i = 0; i < 256; i++)
    {
        data[i] = i;
    }

    result = fal_partition_write(part_dev, 0, data, 256);
    if (result >= 0)
    {
        rt_kprintf("Write data success. Start from 0x%08X, size is %ld.\n", addr, size);
        rt_kprintf("Write data: ");
        for (i = 0; i < size; i++)
        {
            rt_kprintf("%d ", data[i]);
        }
        rt_kprintf(".\n");
    }

    rt_memset(data, 0, 256);
    result = fal_partition_read(part_dev, 0, data, 256);
    if (result >= 0)
    {
        rt_kprintf("Read data success. Start from 0x%08X, size is %ld.\n", addr, size);
        rt_kprintf("Read data: ");
        for (i = 0; i < size; i++)
        {
            rt_kprintf("%d ", data[i]);
        }
        rt_kprintf(".\n");
    }

    result = fal_partition_erase(part_dev, 0, 4096);
    if (result >= 0)
    {
        rt_kprintf("Erase data success.\n");
    }

    rt_memset(data, 0, 256);
    result = fal_partition_read(part_dev, 0, data, 256);
    if (result >= 0)
    {
        rt_kprintf("Read data success. Start from 0x%08X, size is %ld.\n", addr, size);
        rt_kprintf("Read data: ");
        for (i = 0; i < size; i++)
        {
            rt_kprintf("%d ", data[i]);
        }
        rt_kprintf(".\n");
    }
    rt_free(data);

    return 0;
}
MSH_CMD_EXPORT(fal_ops_test, "fal_ops_test");

#endif
#endif/* BSP_USING_ON_CHIP_FLASH */
