/* SPDX-License-Identifier: Apache-2.0 */
#ifndef AB32_DRV_POWER_H
#define AB32_DRV_POWER_H
#include <rtthread.h>
#include <rtdevice.h>

#define AB32_POWER_GET_STATUS  (RT_DEVICE_CTRL_BASE(Miscellaneous) + 0x40)
#define AB32_POWER_SET_WAKE    (RT_DEVICE_CTRL_BASE(Miscellaneous) + 0x41)
#define AB32_POWER_SET_INPUT   (RT_DEVICE_CTRL_BASE(Miscellaneous) + 0x42)
#define AB32_POWER_SET_FILTER  (RT_DEVICE_CTRL_BASE(Miscellaneous) + 0x43)
#define AB32_POWER_SLEEP       (RT_DEVICE_CTRL_BASE(Miscellaneous) + 0x44)
#define AB32_POWER_OFF         (RT_DEVICE_CTRL_BASE(Miscellaneous) + 0x45)
#define AB32_POWER_WAKE_PWKEY   0x10u
#define AB32_POWER_WAKE_VUSB    0x20u
#define AB32_POWER_WAKE_MASK   0x30u
#define AB32_POWER_BUSY_BT     0x01u
#define AB32_POWER_BUSY_AUDIO  0x02u
#define AB32_POWER_BUSY_FM     0x04u
#define AB32_POWER_BUSY_USB    0x08u
#define AB32_POWER_BUSY_TF     0x10u
#define AB32_POWER_BUSY_AUDEC  0x20u

/* All device controls are thread-only. Status is non-destructive: raw latched
 * flags are not an authoritative boot/reset reason and are never acknowledged.
 * PWKEY level is raw (RTCCON bit19), not a debounced/active-low key event. */
struct ab32_power_status {
    rt_uint32_t bridge;
    rt_uint8_t con0, con1, con2, con3, con5, con7, con8, con9;
    rt_uint8_t pwkey_level, vusb_online, wake_enabled, wake_pending;
    rt_uint8_t pwkey_input, pwkey_pullup, filter_enabled, power_rails;
    rt_uint16_t filter_ms;
    rt_uint8_t sleep_supported, poweroff_supported;
    rt_uint32_t blockers;
    rt_uint8_t boot_valid, boot_pending, boot_first_power;
};
struct ab32_power_off {
    rt_uint8_t wake_sources; /* nonzero subset of AB32_POWER_WAKE_MASK */
};
struct ab32_power_wake {
    rt_uint8_t mask, enabled; /* masked update; enabled must be a subset */
};
struct ab32_power_input {
    rt_uint8_t enabled, pullup; /* 0/1; enabled digital input, optional pull-up */
};
/* SET_FILTER takes rt_uint32_t milliseconds: 0(disable), 8,32,128,512.
 * Each SET changes one register, preserves unrelated bits and reads it back.
 * A transfer failure may occur after a write: read status/retry before use.
 * SLEEP takes NULL: align to an RTC second, retain PLL/RAM, enter LPMCON,
 * wake at the next RTC second and compensate the stopped tick (1..2 s total).
 * OFF takes struct ab32_power_off*, never returns on
 * success; wake resets through ROM and normal RTT initialization.
 * Both reject active BT/audio/FM/USB/TF/AUDEC, ISR and scheduler-locked contexts.
 * No automatic PM/tickless scheduling or PLL-off deep sleep. RTC sleep was
 * tested on V2; OFF and current consumption still require hardware tests. */
#endif
