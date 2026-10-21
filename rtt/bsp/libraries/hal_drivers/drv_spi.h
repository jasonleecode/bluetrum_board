/* SPDX-License-Identifier: Apache-2.0 */
#ifndef AB32_DRV_SPI_H
#define AB32_DRV_SPI_H
#include <rtdevice.h>
/* Attach a persistent device to spi1 using fixed reset timing, 8-bit MSB,
 * active-low GPIO CS (or PIN_NONE for external CS). Call once per device
 * during application initialization, before concurrent bus users start.
 * Use rt_spi_send/recv/send_then_recv; simultaneous TX+RX is not supported.
 */
rt_err_t ab32_spi1_attach(struct rt_spi_device *device, const char *name,
                          rt_base_t cs_pin, rt_uint32_t max_hz);
#endif
