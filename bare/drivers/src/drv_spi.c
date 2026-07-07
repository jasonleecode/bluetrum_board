#include "drv_spi.h"

#define BLUE_SPI_MODE_MASTER 1u
#define BLUE_SPI_SET_MAPPING 0x20u
#define BLUE_SPI_ENABLE 0x21u
#define DRV_SPI_DEFAULT_MAPPING 4u

typedef void (*blue_spi_signal_event_t)(uint32_t event);

typedef struct {
    uint16_t api;
    uint16_t drv;
} blue_driver_version_t;

struct blue_drv_spi {
    blue_driver_version_t *(*get_version)(void);
    int32_t (*init)(blue_spi_signal_event_t cb_event);
    int32_t (*deinit)(void);
    int32_t (*power)(uint32_t state);
    int32_t (*send)(const void *data, uint32_t num);
    int32_t (*recv)(void *data, uint32_t num);
    int32_t (*send_byte)(uint8_t data, int32_t timeout);
    int32_t (*recv_byte)(int32_t timeout);
    int32_t (*control)(uint32_t control, uint32_t arg);
};

extern const struct blue_drv_spi blue_spi1;

static drv_spi_event_cb_t spi_event_cb;
static bool spi_initialized;

static void spi_signal_event(uint32_t event)
{
    if (spi_event_cb) {
        spi_event_cb(event);
    }
}

bool drv_spi_init_ex(const drv_spi_cfg_t *cfg)
{
    if (!cfg || cfg->baudrate == 0) {
        return false;
    }

    spi_event_cb = cfg->event_cb;

    if (blue_spi1.init(spi_signal_event) != 0) {
        return false;
    }

    if (blue_spi1.control(BLUE_SPI_MODE_MASTER, cfg->baudrate) != 0) {
        return false;
    }

    if (cfg->mapping != 0 && blue_spi1.control(BLUE_SPI_SET_MAPPING, cfg->mapping) != 0) {
        return false;
    }

    if (blue_spi1.control(BLUE_SPI_ENABLE, 0) != 0) {
        return false;
    }

    (void)cfg->mode;
    spi_initialized = true;
    return true;
}

bool drv_spi_init(drv_spi_mode_t mode, uint32_t baudrate)
{
    const drv_spi_cfg_t cfg = {
        .mode = mode,
        .baudrate = baudrate,
        .mapping = DRV_SPI_DEFAULT_MAPPING,
        .event_cb = 0,
    };

    return drv_spi_init_ex(&cfg);
}

void drv_spi_deinit(void)
{
    if (spi_initialized) {
        (void)blue_spi1.deinit();
        spi_initialized = false;
    }
}

bool drv_spi_write(const uint8_t *buf, uint32_t len)
{
    if (!spi_initialized || !buf || len == 0) {
        return false;
    }

    return blue_spi1.send(buf, len) == 0;
}

bool drv_spi_read(uint8_t *buf, uint32_t len)
{
    if (!spi_initialized || !buf || len == 0) {
        return false;
    }

    return blue_spi1.recv(buf, len) == 0;
}

bool drv_spi_write_byte(uint8_t value, int32_t timeout_ms)
{
    if (!spi_initialized) {
        return false;
    }

    return blue_spi1.send_byte(value, timeout_ms) == 0;
}

int32_t drv_spi_read_byte(int32_t timeout_ms)
{
    if (!spi_initialized) {
        return -1;
    }

    return blue_spi1.recv_byte(timeout_ms);
}

int32_t drv_spi_transfer_byte(uint8_t value, int32_t timeout_ms)
{
    int32_t rx;

    if (!drv_spi_write_byte(value, timeout_ms)) {
        return -1;
    }

    rx = drv_spi_read_byte(timeout_ms);
    if (rx < 0) {
        return -1;
    }

    return rx & 0xff;
}

bool drv_spi_transfer(const uint8_t *tx_buf, uint8_t *rx_buf, uint32_t len)
{
    uint32_t i;

    if (!spi_initialized || !tx_buf || !rx_buf || len == 0) {
        return false;
    }

    for (i = 0; i < len; i++) {
        int32_t rx = drv_spi_transfer_byte(tx_buf[i], 100);
        if (rx < 0) {
            return false;
        }
        rx_buf[i] = (uint8_t)rx;
    }

    return true;
}
