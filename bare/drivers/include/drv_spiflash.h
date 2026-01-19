#pragma once
#include <stdint.h>
#include <stdbool.h>

bool drv_flash_read(uint32_t addr, void *buf, uint32_t len);
bool drv_flash_write(uint32_t addr, const void *buf, uint32_t len);
bool drv_flash_erase(uint32_t addr, uint32_t len);