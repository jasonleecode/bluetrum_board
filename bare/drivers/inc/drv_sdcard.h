#pragma once
#include <stdint.h>
#include <stdbool.h>

/* Native SD0, 1-bit G2: PB0=CMD, PB1=CLK, PB2=DAT0 (board schematic).
 * These pins and SD0 must be exclusively owned by this blocking driver.
 * This replaces the old SPI/set_cs configuration; it never bit-bangs SPI/SD.
 * See docs/lib-analysis/SD_RECOVERY.md for board/startup requirements. */
#define DRV_SDCARD_BLOCK_SIZE 512u

typedef enum {
    DRV_SDCARD_OK = 0,
    DRV_SDCARD_ARGUMENT,
    DRV_SDCARD_NOT_READY,
    DRV_SDCARD_NO_CARD,
    DRV_SDCARD_TIMEOUT,
    DRV_SDCARD_COMMAND_CRC,
    DRV_SDCARD_RESPONSE,
    DRV_SDCARD_DATA_CRC,
    DRV_SDCARD_WRITE_REJECTED,
    DRV_SDCARD_WRITE_PROTECTED,
    DRV_SDCARD_RANGE,
    DRV_SDCARD_UNSUPPORTED,
    DRV_SDCARD_DMA_BUFFER,
    DRV_SDCARD_BUSY
} drv_sdcard_error_t;

typedef struct {
    /* Actual SD source clock; must stay fixed while initialized. Identification
     * requires 200 kHz <= source <= 102.4 MHz. The divider cannot be zero. */
    uint32_t source_clock_hz;
    uint32_t transfer_clock_hz; /* 0: 12 MHz; capped at card rate and 25 MHz. */
    /* Required real monotonic microsecond timer (wrap supported) and delay.
     * Do not use vendor_shim's hal_get_ticks() as a wall clock. */
    uint32_t (*time_us)(void *context);
    void (*delay_us)(void *context, uint32_t us);
    /* Optional socket switches: absent detect means presence is unknown. */
    bool (*card_present)(void *context);
    bool (*write_protected)(void *context);
    void *context;
    /* Caller-owned dedicated 512-byte DMA staging RAM, 32-byte aligned.
     * It must remain alive and unused by the caller until deinit. CPU and
     * SD bus addresses may differ only if a verified platform mapping exists.
     * The SD0 DMA window is 21 bits; arbitrary pointer truncation is invalid.
     * Buffer must be coherent/uncached; this driver has no cache operations. */
    uint8_t *dma_buffer;
    uint32_t dma_address;
    uint32_t command_timeout_us; /* 0: 100 ms, maximum 5 s. */
    uint32_t data_timeout_us;    /* 0: 1 s, maximum 5 s. */
} drv_sdcard_cfg_t;

typedef struct {
    uint64_t block_count;       /* 512-byte sectors, including 2^32 on 2 TB cards. */
    uint32_t clock_hz;          /* Actual divider-derived transfer clock. */
    uint16_t rca;
    bool block_addressing;     /* SDHC/SDXC true; SDSC false. */
    bool write_protected;      /* CSD or socket switch at time of query. */
} drv_sdcard_info_t;

/* Single-threaded/non-reentrant; never call from an ISR or from callbacks.
 * Initialization configures SD0 clock gate and G2 pins, then CMD0/8/ACMD41,
 * CID/RCA/CSD, select, and SDSC block length. No MMC/SDIO/4-bit/UHS support.
 * Caller must first establish the board's clocks, card power and DMA RAM. */
bool drv_sdcard_init(const drv_sdcard_cfg_t *cfg);
void drv_sdcard_deinit(void);
bool drv_sdcard_get_info(drv_sdcard_info_t *info);
drv_sdcard_error_t drv_sdcard_last_error(void);
/* Blocking single-sector commands; unaligned caller buffers are supported.
 * Caller buffers must not overlap the dedicated DMA staging buffer.
 * Reads copy out only after CRC/status success. Writes wait for DAT0 release
 * and CMD13 ready/TRAN. A failed write may still have changed card contents.
 * Transport errors disable SD0 and require initialization again. Range,
 * argument and write-protection errors do not invalidate an initialized card. */
bool drv_sdcard_read_block(uint32_t block, uint8_t *buf512);
bool drv_sdcard_write_block(uint32_t block, const uint8_t *buf512);
