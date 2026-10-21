#pragma once

#include <stdint.h>

/* Partial baseband_platform.o recovery. See the platform recovery notes for
 * the required interrupt dispatcher, RAM vector table and deferred worker. */
void baseband_platform_pass_by_hello(void);
void bthw_irq_init(void);
void bthw_isr_do_plus(void);

/* Preserve the original eight-byte weak object. Only offsets 0 and 1 are
 * understood here; its remaining fields must not be renamed or repurposed. */
extern uint8_t bb_rf_read_inside_upper[8];
