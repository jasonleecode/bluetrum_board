#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    DRV_SPI_MODE0 = 0, /* CPOL=0, CPHA=0 */
    DRV_SPI_MODE1,
    DRV_SPI_MODE2,
    DRV_SPI_MODE3
} drv_spi_mode_t;

typedef void (*drv_spi_event_cb_t)(uint32_t event);

typedef struct {
    drv_spi_mode_t mode;
    uint32_t baudrate;
    uint8_t mapping;
    drv_spi_event_cb_t event_cb;
} drv_spi_cfg_t;

/* 初始化 SPI */
bool drv_spi_init(drv_spi_mode_t mode, uint32_t baudrate);
bool drv_spi_init_ex(const drv_spi_cfg_t *cfg);
void drv_spi_deinit(void);

/* SPI 发送数据 */
bool drv_spi_write(const uint8_t *buf, uint32_t len);

/* SPI 接收数据 */
bool drv_spi_read(uint8_t *buf, uint32_t len);

/* SPI 同时收发 */
bool drv_spi_transfer(const uint8_t *tx_buf, uint8_t *rx_buf, uint32_t len);
bool drv_spi_write_byte(uint8_t value, int32_t timeout_ms);
int32_t drv_spi_read_byte(int32_t timeout_ms);
int32_t drv_spi_transfer_byte(uint8_t value, int32_t timeout_ms);
