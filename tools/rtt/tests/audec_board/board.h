#ifndef TEST_AUDEC_BOARD_H
#define TEST_AUDEC_BOARD_H
#include <stdint.h>
#include <rtthread.h>
#include <rthw.h>
#include <rtdevice.h>
#include <ab32vgx.h>
#include <ab32vg1_hal_rcu.h>
extern volatile rt_uint32_t audec_system[5];
#undef PICCON
#undef PICEN
#undef CLKGAT0
#undef CLKCON2
#undef TMR0CON
#undef TMR0PR
#define PICCON audec_system[0]
#define PICEN audec_system[1]
#define CLKGAT0 audec_system[2]
#define CLKCON2 audec_system[3]
#define TMR0CON audec_system[4]
#define TMR0PR 24000u
#endif
