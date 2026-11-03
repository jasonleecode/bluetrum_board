/* Use actual chip command IDs and bits; only MMIO storage is replaced. */
#ifndef TEST_RTC_BOARD_H
#define TEST_RTC_BOARD_H
#include <rtthread.h>
#include <rthw.h>
#include <rtdevice.h>
#include <ab32vgx.h>
extern volatile rt_uint32_t rtc_mmio[2];
#undef RTCCON
#undef RTCDAT
#define RTCCON rtc_mmio[0]
#define RTCDAT rtc_mmio[1]
#ifdef BSP_USING_POWER
extern volatile rt_uint32_t power_mmio[5];
#undef PICCON
#undef PICEN
#undef TMR0CON
#undef TMR0PR
#undef LPMCON
#define PICCON power_mmio[0]
#define PICEN power_mmio[1]
#define TMR0CON power_mmio[2]
#define TMR0PR power_mmio[3]
#define LPMCON power_mmio[4]
#endif
#endif
