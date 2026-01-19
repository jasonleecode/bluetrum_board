#include "drv_spi.h"

/* libhal.a 符号 */
extern uint32_t blue_spi1;  // SPI1 对象
extern void driver_spi_write(uint32_t *spi_obj, const uint8_t *buf, uint32_t len);
extern void driver_spi_read(uint32_t *spi_obj, uint8_t *buf, uint32_t len);
extern void driver_spi_init(uint32_t *spi_obj, uint32_t baudrate, uint32_t mode);

/* 内部状态 */
static uint32_t *spi_obj = &blue_spi1;

bool drv_spi_init(drv_spi_mode_t mode, uint32_t baudrate)
{
    driver_spi_init(spi_obj, baudrate, mode);
    return true;
}

bool drv_spi_write(const uint8_t *buf, uint32_t len)
{
    if(!buf || len == 0) return false;
    driver_spi_write(spi_obj, buf, len);
    return true;
}

bool drv_spi_read(uint8_t *buf, uint32_t len)
{
    if(!buf || len == 0) return false;
    driver_spi_read(spi_obj, buf, len);
    return true;
}

bool drv_spi_transfer(const uint8_t *tx_buf, uint8_t *rx_buf, uint32_t len)
{
    if(!tx_buf || !rx_buf || len == 0) return false;

    /* 简单实现：先写后读 */
    driver_spi_write(spi_obj, tx_buf, len);
    driver_spi_read(spi_obj, rx_buf, len);
    return true;
}