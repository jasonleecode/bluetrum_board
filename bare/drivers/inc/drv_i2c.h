#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef void (*drv_i2c_set_line_t)(bool high);
typedef bool (*drv_i2c_get_line_t)(void);
typedef void (*drv_i2c_delay_t)(uint32_t usec);

typedef struct {
    drv_i2c_set_line_t set_scl;
    drv_i2c_set_line_t set_sda;
    drv_i2c_get_line_t get_scl;
    drv_i2c_get_line_t get_sda;
    drv_i2c_delay_t delay_us;
    uint32_t clock_hz;
} drv_i2c_cfg_t;

bool drv_i2c_init(const drv_i2c_cfg_t *cfg);
bool drv_i2c_write(uint8_t addr_7bit, const uint8_t *buf, uint32_t len);
bool drv_i2c_read(uint8_t addr_7bit, uint8_t *buf, uint32_t len);
bool drv_i2c_write_read(uint8_t addr_7bit,
                        const uint8_t *tx_buf,
                        uint32_t tx_len,
                        uint8_t *rx_buf,
                        uint32_t rx_len);
