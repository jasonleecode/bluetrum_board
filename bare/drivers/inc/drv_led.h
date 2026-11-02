#ifndef DRV_LED_H
#define DRV_LED_H

#include <stdbool.h>

/* PROUGEN red LED: PE1, active low; V2 P1 pins 3-4 must be bridged. */
void drv_led_init(void);
void drv_led_set(bool on);

#endif
