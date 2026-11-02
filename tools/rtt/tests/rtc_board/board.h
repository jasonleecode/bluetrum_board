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
#endif
