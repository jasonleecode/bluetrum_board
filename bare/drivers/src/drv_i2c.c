#include "drv_i2c.h"

#define DRV_I2C_DEFAULT_CLOCK_HZ 100000u

static drv_i2c_cfg_t i2c_cfg;
static uint32_t i2c_delay_us = 5u;
static bool i2c_ready;

static void i2c_delay(void)
{
    if (i2c_cfg.delay_us) {
        i2c_cfg.delay_us(i2c_delay_us);
    }
}

static void i2c_set_scl(bool high)
{
    i2c_cfg.set_scl(high);
    i2c_delay();
}

static void i2c_set_sda(bool high)
{
    i2c_cfg.set_sda(high);
    i2c_delay();
}

static bool i2c_get_sda(void)
{
    if (i2c_cfg.get_sda) {
        return i2c_cfg.get_sda();
    }
    return true;
}

static void i2c_start(void)
{
    i2c_set_sda(true);
    i2c_set_scl(true);
    i2c_set_sda(false);
    i2c_set_scl(false);
}

static void i2c_stop(void)
{
    i2c_set_sda(false);
    i2c_set_scl(true);
    i2c_set_sda(true);
}

static bool i2c_write_byte(uint8_t value)
{
    uint8_t mask;
    bool ack;

    for (mask = 0x80u; mask != 0; mask >>= 1) {
        i2c_set_sda((value & mask) != 0);
        i2c_set_scl(true);
        i2c_set_scl(false);
    }

    i2c_set_sda(true);
    i2c_set_scl(true);
    ack = !i2c_get_sda();
    i2c_set_scl(false);

    return ack;
}

static uint8_t i2c_read_byte(bool ack)
{
    uint8_t value = 0;
    uint8_t i;

    i2c_set_sda(true);
    for (i = 0; i < 8; i++) {
        value <<= 1;
        i2c_set_scl(true);
        if (i2c_get_sda()) {
            value |= 1u;
        }
        i2c_set_scl(false);
    }

    i2c_set_sda(!ack);
    i2c_set_scl(true);
    i2c_set_scl(false);
    i2c_set_sda(true);

    return value;
}

bool drv_i2c_init(const drv_i2c_cfg_t *cfg)
{
    uint32_t clock_hz;

    if (!cfg || !cfg->set_scl || !cfg->set_sda) {
        return false;
    }

    i2c_cfg = *cfg;
    clock_hz = cfg->clock_hz ? cfg->clock_hz : DRV_I2C_DEFAULT_CLOCK_HZ;
    i2c_delay_us = 500000u / clock_hz;
    if (i2c_delay_us == 0) {
        i2c_delay_us = 1;
    }

    i2c_set_sda(true);
    i2c_set_scl(true);
    i2c_ready = true;

    return true;
}

bool drv_i2c_write(uint8_t addr_7bit, const uint8_t *buf, uint32_t len)
{
    uint32_t i;

    if (!i2c_ready || (!buf && len != 0)) {
        return false;
    }

    i2c_start();
    if (!i2c_write_byte((uint8_t)(addr_7bit << 1))) {
        i2c_stop();
        return false;
    }

    for (i = 0; i < len; i++) {
        if (!i2c_write_byte(buf[i])) {
            i2c_stop();
            return false;
        }
    }

    i2c_stop();
    return true;
}

bool drv_i2c_read(uint8_t addr_7bit, uint8_t *buf, uint32_t len)
{
    uint32_t i;

    if (!i2c_ready || !buf || len == 0) {
        return false;
    }

    i2c_start();
    if (!i2c_write_byte((uint8_t)((addr_7bit << 1) | 1u))) {
        i2c_stop();
        return false;
    }

    for (i = 0; i < len; i++) {
        buf[i] = i2c_read_byte(i + 1u < len);
    }

    i2c_stop();
    return true;
}

bool drv_i2c_write_read(uint8_t addr_7bit,
                        const uint8_t *tx_buf,
                        uint32_t tx_len,
                        uint8_t *rx_buf,
                        uint32_t rx_len)
{
    uint32_t i;

    if (!i2c_ready || (!tx_buf && tx_len != 0) || !rx_buf || rx_len == 0) {
        return false;
    }

    i2c_start();
    if (!i2c_write_byte((uint8_t)(addr_7bit << 1))) {
        i2c_stop();
        return false;
    }
    for (i = 0; i < tx_len; i++) {
        if (!i2c_write_byte(tx_buf[i])) {
            i2c_stop();
            return false;
        }
    }

    i2c_start();
    if (!i2c_write_byte((uint8_t)((addr_7bit << 1) | 1u))) {
        i2c_stop();
        return false;
    }
    for (i = 0; i < rx_len; i++) {
        rx_buf[i] = i2c_read_byte(i + 1u < rx_len);
    }

    i2c_stop();
    return true;
}
