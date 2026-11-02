/* Actual BSP HAL declarations with host-backed register storage. */
#ifndef TEST_GPIO_BOARD_H
#define TEST_GPIO_BOARD_H
#include <rtthread.h>
#include <rthw.h>
#include <rtdevice.h>
#include <ab32vgx.h>
extern volatile unsigned long gpio_ports[4][13], gpio_wkup[5], gpio_maps[3];
#undef GPIOA_BASE
#undef GPIOB_BASE
#undef GPIOE_BASE
#undef GPIOF_BASE
#define GPIOA_BASE gpio_ports[0]
#define GPIOB_BASE gpio_ports[1]
#define GPIOE_BASE gpio_ports[2]
#define GPIOF_BASE gpio_ports[3]
#undef GPIOA
#undef GPIOB
#undef GPIOE
#undef GPIOF
#define GPIOA gpio_ports[0][GPIOx]
#define GPIOB gpio_ports[1][GPIOx]
#define GPIOE gpio_ports[2][GPIOx]
#define GPIOF gpio_ports[3][GPIOx]
#undef WKUPCON
#undef WKUPEDG
#undef WKUPCPND
#undef PORTINTEN
#undef PORTINTEDG
#define WKUPCON gpio_wkup[0]
#define WKUPEDG gpio_wkup[1]
#define WKUPCPND gpio_wkup[2]
#define PORTINTEN gpio_wkup[3]
#define PORTINTEDG gpio_wkup[4]
#undef FUNCMCONx
#define FUNCMCONx(x) gpio_maps[x]
#endif
