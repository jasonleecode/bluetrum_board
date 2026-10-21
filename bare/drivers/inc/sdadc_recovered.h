#pragma once
#include <stdint.h>
/* Raw vendor ABI: encoded channel, packed analog/digital gain, DMA count.
 * Counts and address extents must be validated by the board/caller. */
int sdadc_set_channel(uint8_t channel);
int sdadc_set_sample_rate(uint32_t hz);
int sdadc_set_gain(uint16_t gain);
int sdadc_set_dma_samples(uint16_t count);
uint8_t sdadc_get_channel(void);
uint8_t sdadc_get_sample_rate(void); /* returns index, NOT Hz */
uint16_t sdadc_get_gain(void);
uint16_t sdadc_get_dma_samples(void);
int sdadc_start(void *dma_address);
int sdadc_exit(void);
void sdadc_dma_notice(uint32_t event);
void sdadc_isr(void);
