#include "drv_spiflash.h"

/* libhal */
extern void os_spiflash_read(uint32_t addr, void *buf, uint32_t len);
extern void os_spiflash_program(uint32_t addr, const void *buf, uint32_t len);
extern void os_spiflash_erase(uint32_t addr, uint32_t len);

bool drv_flash_read(uint32_t addr, void *buf, uint32_t len)
{
    os_spiflash_read(addr, buf, len);
    return true;
}

bool drv_flash_write(uint32_t addr, const void *buf, uint32_t len)
{
    os_spiflash_program(addr, buf, len);
    return true;
}

bool drv_flash_erase(uint32_t addr, uint32_t len)
{
    os_spiflash_erase(addr, len);
    return true;
}