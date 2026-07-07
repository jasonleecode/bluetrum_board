#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef void (*drv_sdcard_cs_t)(bool selected);

typedef struct {
    drv_sdcard_cs_t set_cs;
    uint32_t spi_baudrate;
} drv_sdcard_cfg_t;

bool drv_sdcard_init(const drv_sdcard_cfg_t *cfg);
bool drv_sdcard_read_block(uint32_t block, uint8_t *buf512);
bool drv_sdcard_write_block(uint32_t block, const uint8_t *buf512);
