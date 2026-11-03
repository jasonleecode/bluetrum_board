/* SPDX-License-Identifier: Apache-2.0
 * AB32 power: retained-clock sleep and RTC-domain reset-on-wake shutdown.
 * Critical entry/terminal paths must execute from SRAM, not the Flash cache.
 */
#include "board.h"
#include "drv_power.h"
#include "drv_rtc_internal.h"
#ifdef BSP_USING_POWER
#ifndef BSP_USING_ONCHIP_RTC
#error "Power driver requires the serialized native RTC bridge"
#endif
static struct rt_device power_device;
static rt_bool_t power_ready;
static rt_uint8_t boot_valid, boot_pending, boot_first_power;
static rt_uint32_t sleep_entries;
#define POWER_RAM rt_section(".irq.power") __attribute__((noinline))

/* Called with the scheduler locked (IRQs still enabled). These observers
 * neither block nor acquire driver locks. A lock held by another thread is
 * busy even before that driver has enabled its hardware. */
static rt_uint32_t power_blockers(void)
{
    rt_uint32_t bits = 0;
#ifdef BSP_USING_BTCTRL
    extern rt_bool_t ab32_btctrl_power_busy(void);
    if (ab32_btctrl_power_busy()) bits |= AB32_POWER_BUSY_BT;
#endif
#ifdef BSP_USING_AUDIO
    extern rt_bool_t ab32_sound_power_busy(void);
    if (ab32_sound_power_busy()) bits |= AB32_POWER_BUSY_AUDIO;
#endif
#ifdef BSP_USING_FMRX
    extern rt_bool_t ab32_fmrx_power_busy(void);
    if (ab32_fmrx_power_busy()) bits |= AB32_POWER_BUSY_FM;
#endif
#ifdef BSP_USING_USB_CDC
    extern rt_bool_t ab32_usb_power_busy(void);
    if (ab32_usb_power_busy()) bits |= AB32_POWER_BUSY_USB;
#endif
#ifdef BSP_USING_TFCARD
    extern rt_bool_t ab32_tf_power_busy(void);
    if (ab32_tf_power_busy()) bits |= AB32_POWER_BUSY_TF;
#endif
    return bits;
}

static rt_err_t power_sleep_once(void *unused)
{
    (void)unused;
    sleep_entries = 0;
    rt_uint32_t second;
    rt_tick_t edge;
    if (!(PICCON & 1u) || !(PICEN & BIT(IRQ_TMR0_VECTOR)) ||
        (TMR0CON & 0x81u) != 0x81u || !TMR0PR || (LPMCON & 7u))
        return -RT_EBUSY;
    rt_enter_critical();
    rt_uint32_t blockers = power_blockers();
    rt_exit_critical();
    if (blockers) return -RT_EBUSY;
    rt_err_t rc = ab32_irtc_align_second_locked(&second, &edge);
    if (rc) return rc;
    rt_enter_critical();
    rc = power_blockers() ? -RT_EBUSY : ab32_irtc_sleep_locked(second, edge, &sleep_entries);
    rt_exit_critical();
    return rc;
}

/* Preserve IRQ and read-only status bits; caller owns the RTC mutex. */
POWER_RAM static void power_bridge_policy(rt_uint32_t policy)
{
    rt_base_t level = rt_hw_interrupt_disable();
    RTCCON = (RTCCON & ~0x1e0u) | (policy & 0x1e0u);
    rt_hw_interrupt_enable(level);
}
POWER_RAM __attribute__((noreturn)) static void power_cutoff(rt_uint8_t con3)
{
    (void)rt_hw_interrupt_disable();
    RTCCON = (RTCCON & ~0x1e0u) | RTC_CON_WK_RST_ENABLE;
    ab32_irtc_cutoff_locked(con3);
}

static rt_err_t power_off_locked(void *arg)
{
    const struct ab32_power_off *off = arg;
    rt_uint8_t con0, con1, con3, con5;
    rt_uint32_t bridge;
    rt_err_t rc;
    rt_enter_critical(); /* close start/stop races across the integrated drivers */
    if (power_blockers()) { rc = -RT_EBUSY; goto out; }
#define READ_OFF(n) do { rc = ab32_irtc_read8_locked(RTCCON##n##_CMD, &con##n); if (rc) goto out; } while (0)
    READ_OFF(0); READ_OFF(1); READ_OFF(3); READ_OFF(5);
#undef READ_OFF
    bridge = RTCCON;
    if ((con3 & 7u) != 7u || !(con0 & RTC_CON0_VDD_ENABLE))
    { rc = -RT_EIO; goto out; }
    if ((off->wake_sources & AB32_POWER_WAKE_PWKEY) &&
        (!(con1 & 1u) || (con1 & 0x20u))) { rc = -RT_EINVAL; goto out; }
    if (((off->wake_sources & AB32_POWER_WAKE_PWKEY) && !(bridge & RTC_CON_WK_PIN_STATE)) ||
        ((off->wake_sources & AB32_POWER_WAKE_VUSB) && (bridge & RTC_CON_VUSB_OLINE)))
    { rc = -RT_EBUSY; goto out; }

    /* Disable reset-on-wake while rearming sources. Toggling WK/VUSB enable
     * rearms their latched wake; this is intentionally destructive only for
     * an explicit OFF, never for status/sleep/configuration commands. */
    power_bridge_policy(0);
    rc = ab32_irtc_shutdown_update_locked(RTCCON3_CMD, 0xf0, 0);
    if (!rc) rc = ab32_irtc_shutdown_update_locked(RTCCON5_CMD, 3, 1);
    if (!rc) rc = ab32_irtc_shutdown_update_locked(RTCCON0_CMD, 4, 0);
    if (!rc)
    {
        /* Recheck physical levels immediately before the terminal write. */
        if (((off->wake_sources & AB32_POWER_WAKE_PWKEY) && !(RTCCON & RTC_CON_WK_PIN_STATE)) ||
            ((off->wake_sources & AB32_POWER_WAKE_VUSB) && (RTCCON & RTC_CON_VUSB_OLINE)))
            rc = -RT_EBUSY;
        else power_cutoff((con3 & 8u) | off->wake_sources);
    }
    /* Only reachable BEFORE commit. Attempt every rollback even if one
     * fails. Clock/time/count/RAM and CON9 are never changed. */
    rt_err_t restore = ab32_irtc_shutdown_update_locked(RTCCON0_CMD, 4, con0 & 4u);
    if (ab32_irtc_shutdown_update_locked(RTCCON5_CMD, 3, con5 & 3u)) restore = -RT_EIO;
    if (ab32_irtc_shutdown_update_locked(RTCCON3_CMD, 0xf0, con3 & 0xf0u)) restore = -RT_EIO;
    power_bridge_policy(bridge);
    if (restore) rc = -RT_EIO; /* do not claim successful rollback */
out:
    rt_exit_critical();
    return rc;
}

static rt_err_t power_snapshot(void *arg)
{
    struct ab32_power_status s = {0};
    rt_err_t rc;
#define READ_CON(n) do { rc = ab32_irtc_read8_locked(RTCCON##n##_CMD, &s.con##n); if (rc) return rc; } while (0)
    READ_CON(0); READ_CON(1); READ_CON(2); READ_CON(3);
    READ_CON(5); READ_CON(7); READ_CON(8); READ_CON(9);
#undef READ_CON
    s.bridge = RTCCON;
    s.pwkey_level = !!(s.bridge & RTC_CON_WK_PIN_STATE);
    s.vusb_online = !!(s.bridge & RTC_CON_VUSB_OLINE);
    s.wake_enabled = s.con3 & 0xf0u;
    s.wake_pending = s.con8 & 0x1fu; /* raw RTC-domain latched flags */
    s.pwkey_input = s.con1 & 1u;
    s.pwkey_pullup = !!(s.con1 & 0x10u);
    s.filter_enabled = !!(s.con7 & 4u);
    s.filter_ms = s.filter_enabled ? (8u << (2 * (s.con7 & 3u))) : 0;
    s.power_rails = s.con3 & 7u;
    s.sleep_supported = s.poweroff_supported = 1; /* implemented, not a measured claim */
    rt_enter_critical();
    s.blockers = power_blockers();
    rt_exit_critical();
    s.boot_valid = boot_valid; s.boot_pending = boot_pending;
    s.boot_first_power = boot_first_power;
    rt_memcpy(arg, &s, sizeof(s)); /* publish only a complete successful snapshot */
    return RT_EOK;
}
struct power_update { rt_uint8_t command, mask, value; };
static rt_err_t power_update(void *arg)
{
    const struct power_update *u = arg;
    return ab32_irtc_update8_locked(u->command, u->mask, u->value);
}
static rt_err_t power_control(rt_device_t dev, int cmd, void *arg)
{
    (void)dev;
    if (!power_ready) return -RT_EIO;
    if (rt_interrupt_get_nest() || !rt_thread_self() || rt_critical_level() || !(PICCON & 1u))
        return -RT_EBUSY;
    if (cmd == AB32_POWER_SLEEP || cmd == AB32_POWER_OFF)
    {
        if (cmd == AB32_POWER_SLEEP)
        {
            if (arg) return -RT_EINVAL;
            return ab32_irtc_transaction(power_sleep_once, RT_NULL);
        }
        if (!arg) return -RT_EINVAL;
        const struct ab32_power_off *off = arg;
        if (!off->wake_sources || (off->wake_sources & ~AB32_POWER_WAKE_MASK)) return -RT_EINVAL;
        return ab32_irtc_transaction(power_off_locked, arg);
    }
    if (cmd != AB32_POWER_GET_STATUS && cmd != AB32_POWER_SET_WAKE &&
        cmd != AB32_POWER_SET_INPUT && cmd != AB32_POWER_SET_FILTER) return -RT_ENOSYS;
    if (!arg) return -RT_EINVAL;
    if (cmd == AB32_POWER_GET_STATUS) return ab32_irtc_transaction(power_snapshot, arg);
    struct power_update u;
    if (cmd == AB32_POWER_SET_WAKE)
    {
        const struct ab32_power_wake *w = arg;
        if (!w->mask || (w->mask & ~AB32_POWER_WAKE_MASK) ||
            (w->enabled & ~w->mask)) return -RT_EINVAL;
        u = (struct power_update){RTCCON3_CMD, w->mask, w->enabled};
    }
    else if (cmd == AB32_POWER_SET_INPUT)
    {
        const struct ab32_power_input *i = arg;
        if (i->enabled > 1 || i->pullup > 1) return -RT_EINVAL;
        /* Avoid simultaneous pull-up and pull-down. Preserve pull-up strength. */
        u = (struct power_update){RTCCON1_CMD, 0x13, i->enabled | (i->pullup << 4)};
    }
    else
    {
        rt_uint32_t ms = *(rt_uint32_t *)arg;
        rt_uint8_t setting;
        switch (ms)
        {
        case 0: setting = 0; break;
        case 8: setting = 4; break;
        case 32: setting = 5; break;
        case 128: setting = 6; break;
        case 512: setting = 7; break;
        default: return -RT_EINVAL;
        }
        u = (struct power_update){RTCCON7_CMD, 7, setting};
    }
    return ab32_irtc_transaction(power_update, &u);
}
static const struct rt_device_ops power_ops = {.control = power_control};
int ab32_power_init(void)
{
    power_device.type = RT_Device_Class_Miscellaneous;
    power_device.ops = &power_ops;
    rt_err_t rc = rt_device_register(&power_device, "power0", RT_DEVICE_FLAG_RDWR);
    if (!rc) power_ready = RT_TRUE;
    return rc; /* registration only: no boot MMIO and no allocation */
}
INIT_DEVICE_EXPORT(ab32_power_init);

/* After all devices (including RTC mutex) are registered, before app threads.
 * Raw evidence only; neither clears flags nor claims an exact reset cause. */
static int power_capture_boot(void)
{
    struct ab32_power_status s;
    rt_err_t rc = power_control(&power_device, AB32_POWER_GET_STATUS, &s);
    if (!rc) { boot_pending = s.wake_pending; boot_first_power = !!(s.con0 & 0x80u); boot_valid = 1; }
    return rc;
}
INIT_COMPONENT_EXPORT(power_capture_boot);

#ifdef RT_USING_FINSH
#include <finsh.h>
#include <string.h>
static int power_onoff(const char *s)
{
    return !strcmp(s, "on") ? 1 : !strcmp(s, "off") ? 0 : -1;
}
static int pmctl(int argc, char **argv)
{
    rt_err_t rc;
    if (argc == 2 && !strcmp(argv[1], "status"))
    {
        struct ab32_power_status s;
        rc = power_control(&power_device, AB32_POWER_GET_STATUS, &s);
        if (rc) return rc;
        rt_kprintf("power0: pwkey_level=%u vusb=%u input=%u pullup=%u filter_ms=%u\n",
            s.pwkey_level, s.vusb_online, s.pwkey_input, s.pwkey_pullup, s.filter_ms);
        rt_kprintf("wake_enable=%02x wake_pending=%02x rails=%x first_power=%u sleep=retained-clock off=reset-on-wake\n",
            s.wake_enabled, s.wake_pending, s.power_rails, !!(s.con0 & 0x80));
        rt_kprintf("blockers=%02x (BT=01 audio=02 FM=04 USB=08 TF=10) boot_valid=%u boot_pending=%02x boot_first=%u\n",
            (unsigned)s.blockers, s.boot_valid, s.boot_pending, s.boot_first_power);
        rt_kprintf("RTC bridge=%08x CON0=%02x CON1=%02x CON2=%02x CON3=%02x CON5=%02x CON7=%02x CON8=%02x CON9=%02x\n",
            (unsigned)s.bridge, s.con0, s.con1, s.con2, s.con3, s.con5, s.con7, s.con8, s.con9);
        return RT_EOK;
    }
    if (argc == 2 && !strcmp(argv[1], "sleep"))
    {
        rt_tick_t start = rt_tick_get();
        rc = power_control(&power_device, AB32_POWER_SLEEP, RT_NULL);
        rt_kprintf("sleep returned: %d tick_delta=%u entries=%u (RTC 1s wake, tick compensated)\n",
            rc, (unsigned)(rt_tick_get() - start), (unsigned)sleep_entries);
        return rc;
    }
    if ((argc == 2 || argc == 3) && !strcmp(argv[1], "off"))
    {
        struct ab32_power_off off = {AB32_POWER_WAKE_PWKEY};
        if (argc == 3)
        {
            if (!strcmp(argv[2], "vusb")) off.wake_sources = AB32_POWER_WAKE_VUSB;
            else if (!strcmp(argv[2], "both")) off.wake_sources = AB32_POWER_WAKE_MASK;
            else if (strcmp(argv[2], "pwkey")) return -RT_EINVAL;
        }
        rt_kprintf("Power-off request, wake=%02x; wake restarts RTT.\n", off.wake_sources);
        /* Drain the polling console before cutting power; no logging in the
         * terminal SRAM path. This delay is outside all locks. */
        rt_thread_mdelay(20);
        rc = power_control(&power_device, AB32_POWER_OFF, &off);
        rt_kprintf("Power-off aborted: %d; inspect pmctl status.\n", rc);
        return rc;
    }
    if (argc == 4 && !strcmp(argv[1], "wake"))
    {
        int enable = power_onoff(argv[3]);
        struct ab32_power_wake w = {0};
        if (!strcmp(argv[2], "pwkey")) w.mask = AB32_POWER_WAKE_PWKEY;
        else if (!strcmp(argv[2], "vusb")) w.mask = AB32_POWER_WAKE_VUSB;
        if (!w.mask || enable < 0) return -RT_EINVAL;
        w.enabled = enable ? w.mask : 0;
        return power_control(&power_device, AB32_POWER_SET_WAKE, &w);
    }
    if (argc == 4 && !strcmp(argv[1], "input"))
    {
        int enable = power_onoff(argv[2]), pullup = power_onoff(argv[3]);
        if (enable < 0 || pullup < 0) return -RT_EINVAL;
        struct ab32_power_input i = {enable, pullup};
        return power_control(&power_device, AB32_POWER_SET_INPUT, &i);
    }
    if (argc == 3 && !strcmp(argv[1], "filter"))
    {
        rt_uint32_t ms;
        if (!strcmp(argv[2], "off")) ms = 0;
        else if (!strcmp(argv[2], "8")) ms = 8;
        else if (!strcmp(argv[2], "32")) ms = 32;
        else if (!strcmp(argv[2], "128")) ms = 128;
        else if (!strcmp(argv[2], "512")) ms = 512;
        else return -RT_EINVAL;
        return power_control(&power_device, AB32_POWER_SET_FILTER, &ms);
    }
    rt_kprintf("pmctl status | wake <pwkey|vusb> <on|off> | input <on|off> <pullup:on|off>\n"
               "pmctl filter <off|8|32|128|512> | sleep | off [pwkey|vusb|both]\n");
    return -RT_EINVAL;
}
MSH_CMD_EXPORT(pmctl, Power diagnostics and wake configuration);
#endif
#endif
