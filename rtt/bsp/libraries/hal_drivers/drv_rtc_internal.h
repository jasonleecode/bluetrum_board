/* SPDX-License-Identifier: Apache-2.0
 * Private RTC/PM bridge sharing. Never call the locked helpers independently.
 * Transaction callback runs in THREAD context with rtc_lock held, IRQs enabled.
 */
#ifndef AB32_DRV_RTC_INTERNAL_H
#define AB32_DRV_RTC_INTERNAL_H
#include <rtthread.h>
rt_err_t ab32_irtc_transaction(rt_err_t (*operation)(void *), void *arg);
rt_err_t ab32_irtc_read8_locked(rt_uint8_t command, rt_uint8_t *value);
rt_err_t ab32_irtc_update8_locked(rt_uint8_t command, rt_uint8_t mask, rt_uint8_t value);
rt_err_t ab32_irtc_align_second_locked(rt_uint32_t *second, rt_tick_t *edge);
rt_err_t ab32_irtc_sleep_locked(rt_uint32_t second, rt_tick_t edge, rt_uint32_t *entries);
/* Shutdown-only preparation; still excludes rails, pending and reset policy. */
rt_err_t ab32_irtc_shutdown_update_locked(rt_uint8_t command, rt_uint8_t mask, rt_uint8_t value);
/* SRAM terminal path. Caller owns rtc_lock and has disabled IRQs. Does not
 * return: commits rail removal + LPM entry, or resets on transfer failure. */
void ab32_irtc_cutoff_locked(rt_uint8_t con3) __attribute__((noreturn));
#endif
