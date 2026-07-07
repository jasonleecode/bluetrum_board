#include "drv_sdcard.h"
#include "drv_spi.h"

#define SD_BLOCK_SIZE 512u
#define SD_CMD0 0u
#define SD_CMD8 8u
#define SD_CMD17 17u
#define SD_CMD24 24u
#define SD_CMD55 55u
#define SD_CMD58 58u
#define SD_ACMD41 41u
#define SD_TOKEN_START_BLOCK 0xfeu
#define SD_TOKEN_DATA_ACCEPTED 0x05u
#define SD_IDLE_STATE 0x01u
#define SD_READY_STATE 0x00u
#define SD_CMD_TIMEOUT 1000u
#define SD_DEFAULT_SPI_BAUDRATE 400000u

static drv_sdcard_cs_t sd_set_cs;
static bool sd_ready;
static bool sd_block_addressing;

static void sd_select(bool selected)
{
    if (sd_set_cs) {
        sd_set_cs(selected);
    }
}

static uint8_t sd_spi(uint8_t value)
{
    int32_t rx = drv_spi_transfer_byte(value, 100);
    if (rx < 0) {
        return 0xffu;
    }
    return (uint8_t)rx;
}

static void sd_clock_idle(uint32_t cycles)
{
    while (cycles--) {
        (void)sd_spi(0xffu);
    }
}

static uint8_t sd_wait_response(void)
{
    uint32_t i;

    for (i = 0; i < SD_CMD_TIMEOUT; i++) {
        uint8_t r = sd_spi(0xffu);
        if ((r & 0x80u) == 0) {
            return r;
        }
    }

    return 0xffu;
}

static uint8_t sd_command(uint8_t cmd, uint32_t arg, uint8_t crc)
{
    sd_spi((uint8_t)(0x40u | cmd));
    sd_spi((uint8_t)(arg >> 24));
    sd_spi((uint8_t)(arg >> 16));
    sd_spi((uint8_t)(arg >> 8));
    sd_spi((uint8_t)arg);
    sd_spi(crc);

    return sd_wait_response();
}

static uint8_t sd_command_selected(uint8_t cmd, uint32_t arg, uint8_t crc)
{
    uint8_t r;

    sd_select(true);
    sd_spi(0xffu);
    r = sd_command(cmd, arg, crc);
    sd_select(false);
    sd_spi(0xffu);

    return r;
}

static bool sd_app_command(uint8_t acmd, uint32_t arg, uint8_t expected)
{
    uint8_t r;

    r = sd_command_selected(SD_CMD55, 0, 0xffu);
    if (r > SD_IDLE_STATE) {
        return false;
    }

    r = sd_command_selected(acmd, arg, 0xffu);
    return r == expected;
}

bool drv_sdcard_init(const drv_sdcard_cfg_t *cfg)
{
    uint32_t i;
    uint8_t r;
    uint8_t ocr0;

    if (!cfg || !cfg->set_cs) {
        return false;
    }

    sd_set_cs = cfg->set_cs;
    sd_ready = false;
    sd_block_addressing = false;

    if (!drv_spi_init(DRV_SPI_MODE0, cfg->spi_baudrate ? cfg->spi_baudrate : SD_DEFAULT_SPI_BAUDRATE)) {
        return false;
    }

    sd_select(false);
    sd_clock_idle(10);

    for (i = 0; i < SD_CMD_TIMEOUT; i++) {
        if (sd_command_selected(SD_CMD0, 0, 0x95u) == SD_IDLE_STATE) {
            break;
        }
    }
    if (i == SD_CMD_TIMEOUT) {
        return false;
    }

    sd_select(true);
    sd_spi(0xffu);
    r = sd_command(SD_CMD8, 0x000001aau, 0x87u);
    if (r == SD_IDLE_STATE) {
        sd_clock_idle(4);
    }
    sd_select(false);
    sd_spi(0xffu);

    for (i = 0; i < SD_CMD_TIMEOUT; i++) {
        if (sd_app_command(SD_ACMD41, 0x40000000u, SD_READY_STATE)) {
            break;
        }
    }
    if (i == SD_CMD_TIMEOUT) {
        return false;
    }

    sd_select(true);
    sd_spi(0xffu);
    r = sd_command(SD_CMD58, 0, 0xffu);
    ocr0 = sd_spi(0xffu);
    sd_clock_idle(3);
    sd_select(false);
    sd_spi(0xffu);

    if (r != SD_READY_STATE) {
        return false;
    }

    sd_block_addressing = (ocr0 & 0x40u) != 0;
    sd_ready = true;
    return true;
}

bool drv_sdcard_read_block(uint32_t block, uint8_t *buf512)
{
    uint32_t i;
    uint32_t arg;
    uint8_t token;

    if (!sd_ready || !buf512) {
        return false;
    }

    arg = sd_block_addressing ? block : block * SD_BLOCK_SIZE;
    sd_select(true);
    sd_spi(0xffu);
    if (sd_command(SD_CMD17, arg, 0xffu) != SD_READY_STATE) {
        sd_select(false);
        sd_spi(0xffu);
        return false;
    }

    for (i = 0; i < SD_CMD_TIMEOUT; i++) {
        token = sd_spi(0xffu);
        if (token == SD_TOKEN_START_BLOCK) {
            break;
        }
    }
    if (i == SD_CMD_TIMEOUT) {
        sd_select(false);
        sd_spi(0xffu);
        return false;
    }

    for (i = 0; i < SD_BLOCK_SIZE; i++) {
        buf512[i] = sd_spi(0xffu);
    }
    sd_clock_idle(2);
    sd_select(false);
    sd_spi(0xffu);

    return true;
}

bool drv_sdcard_write_block(uint32_t block, const uint8_t *buf512)
{
    uint32_t i;
    uint32_t arg;
    uint8_t response;

    if (!sd_ready || !buf512) {
        return false;
    }

    arg = sd_block_addressing ? block : block * SD_BLOCK_SIZE;
    sd_select(true);
    sd_spi(0xffu);
    if (sd_command(SD_CMD24, arg, 0xffu) != SD_READY_STATE) {
        sd_select(false);
        sd_spi(0xffu);
        return false;
    }

    sd_spi(SD_TOKEN_START_BLOCK);
    for (i = 0; i < SD_BLOCK_SIZE; i++) {
        sd_spi(buf512[i]);
    }
    sd_clock_idle(2);

    response = sd_spi(0xffu) & 0x1fu;
    if (response != SD_TOKEN_DATA_ACCEPTED) {
        sd_select(false);
        sd_spi(0xffu);
        return false;
    }

    for (i = 0; i < SD_CMD_TIMEOUT; i++) {
        if (sd_spi(0xffu) == 0xffu) {
            break;
        }
    }

    sd_select(false);
    sd_spi(0xffu);
    return i != SD_CMD_TIMEOUT;
}
