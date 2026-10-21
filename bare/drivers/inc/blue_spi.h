#pragma once
#include <stdint.h>

/* Recovered driver_spi.o ABI. This is hardware SPI1, not GPIO bit-banging.
 * Host/slave/simplex are controller roles, NOT CPOL/CPHA mode numbers.
 */
#define BLUE_SPI_MODE_INACTIVE        0x00u
#define BLUE_SPI_MODE_MASTER          0x01u
#define BLUE_SPI_MODE_SLAVE           0x02u
#define BLUE_SPI_MODE_MASTER_SIMPLEX  0x03u
#define BLUE_SPI_MODE_SLAVE_SIMPLEX   0x04u /* Header advertised; object rejects. */
#define BLUE_SPI_SET_MAPPING          0x20u
#define BLUE_SPI_ENABLE               0x21u
#define BLUE_SPI_EVENT_COMPLETE       1u

typedef void (*blue_spi_signal_event_t)(uint32_t event);
typedef struct {
    uint16_t api;
    uint16_t drv;
} blue_spi_version_t;

struct blue_drv_spi {
    blue_spi_version_t *(*get_version)(void);
    int32_t (*init)(blue_spi_signal_event_t cb_event);
    int32_t (*deinit)(void);
    int32_t (*power)(uint32_t state);
    /* DMA launch only: keep buffer valid until event 1. Count is written as-is.
     * Caller must serialize DMA, byte operations and configuration changes. */
    int32_t (*send)(const void *data, uint32_t num);
    int32_t (*recv)(void *data, uint32_t num);
    /* Actual object compares timeout as signed 32-bit, <= 0 waits forever.
     * Historical SDK header uses uint16_t; use 1..65535 for shared behavior.
     * Units follow hal_get_ticks(), NOT necessarily milliseconds. */
    int32_t (*send_byte)(uint8_t data, int32_t timeout);
    int32_t (*recv_byte)(int32_t timeout);
    int32_t (*control)(uint32_t control, uint32_t arg);
};

extern const struct blue_drv_spi blue_spi1;
