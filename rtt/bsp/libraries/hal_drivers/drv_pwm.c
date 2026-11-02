/*
 * Copyright (c) 2020-2021, Bluetrum Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2021-01-28     greedyhao         first version
 */

#include <board.h>

#ifdef RT_USING_PWM
#include "pwm_config.h"

/* Timer duty registers and LPWM period are 16 bit. No timer prescaler is
 * documented: reject unrepresentable timings instead of inventing a divider.
 * All callbacks are thread-only and serialized (LPWM shares period/polarity). */
#if (defined(BSP_USING_T3_PWM) && (defined(BSP_USING_TIM3) || defined(BSP_USING_IRRX))) || \
    (defined(BSP_USING_T4_PWM) && defined(BSP_USING_TIM4)) || \
    (defined(BSP_USING_T5_PWM) && defined(BSP_USING_TIM5))
#error "PWM timer already owned by hwtimer/IR"
#endif
#if defined(BSP_USING_T3_PWM0) && (defined(BSP_USING_TFCARD) || defined(BSP_USING_SDIO))
#error "Timer3 PWM0 conflicts with TF/SD PB0"
#endif
#if defined(BSP_USING_T5_PWM0) && defined(BSP_USING_I2S) && BSP_I2S_MAPPING == 1
#error "Timer5 PWM0 conflicts with I2S PE1"
#endif
static struct rt_mutex pwm_lock;
static rt_bool_t pwm_ready;

void hal_pwm_mspinit(void);

enum
{
#ifdef BSP_USING_T3_PWM
    T3_PWM_INDEX,
#endif
#ifdef BSP_USING_T4_PWM
    T4_PWM_INDEX,
#endif
#ifdef BSP_USING_T5_PWM
    T5_PWM_INDEX,
#endif
#ifdef BSP_USING_LPWM0
    LPWM0_INDEX,
#endif
#ifdef BSP_USING_LPWM1
    LPWM1_INDEX,
#endif
#ifdef BSP_USING_LPWM2
    LPWM2_INDEX,
#endif
#ifdef BSP_USING_LPWM3
    LPWM3_INDEX,
#endif
};

struct ab32_pwm
{
    struct rt_device_pwm pwm_device;
    hal_sfr_t   pwm_handle;
    char *name;
    rt_uint8_t  channel;
    rt_uint32_t period;
    rt_uint32_t pulse[3];
    rt_uint8_t configured, enabled;
};

static struct ab32_pwm ab32_pwm_obj[] =
{
#ifdef BSP_USING_T3_PWM
    T3_PWM_CONFIG,
#endif
#ifdef BSP_USING_T4_PWM
    T4_PWM_CONFIG,
#endif
#ifdef BSP_USING_T5_PWM
    T5_PWM_CONFIG,
#endif
#ifdef BSP_USING_LPWM0
    LPWM0_CONFIG,
#endif
#ifdef BSP_USING_LPWM1
    LPWM1_CONFIG,
#endif
#ifdef BSP_USING_LPWM2
    LPWM2_CONFIG,
#endif
#ifdef BSP_USING_LPWM3
    LPWM3_CONFIG,
#endif
};

/* Legacy BSP channels are single-bit selectors: 1/2/4 = PWM0/1/2.
 * Each lpwmN device exposes channel 1 only. Combined masks are rejected. */
static rt_err_t pwm_control_locked(struct ab32_pwm *obj, int cmd,
                                   struct rt_pwm_configuration *cfg)
{
    hal_sfr_t regs = obj->pwm_handle;
    rt_bool_t lp = obj->name[0] == 'l';
    unsigned ch = cfg->channel, index;
    if (!ch || (ch & (ch - 1)) || !(obj->channel & ch)) return -RT_EINVAL;
    index = ch == 1 ? 0 : ch == 2 ? 1 : 2;
    unsigned output = lp ? (unsigned)(obj->name[4] - '0') : index;
    if (cfg->complementary || cmd == PWMN_CMD_ENABLE) return -RT_ENOSYS;
    switch (cmd)
    {
    case PWMN_CMD_DISABLE:
        return RT_EOK;
    case PWM_CMD_ENABLE:
        if (!(obj->configured & ch)) return -RT_EINVAL;
        if (lp) regs[PWMxCON] |= BIT(output);
        else regs[TMRxCON] |= BIT(9 + output) | BIT(0);
        obj->enabled |= ch;
        return RT_EOK;
    case PWM_CMD_DISABLE:
        if (lp) regs[PWMxCON] &= ~BIT(output);
        else
        {
            regs[TMRxCON] &= ~BIT(9 + output);
            if (!(regs[TMRxCON] & (7u << 9))) regs[TMRxCON] &= ~BIT(0);
        }
        obj->enabled &= ~ch;
        return RT_EOK;
    case PWM_CMD_GET:
        if (!(obj->configured & ch)) return -RT_EIO;
        cfg->period = obj->period;
        cfg->pulse = obj->pulse[index];
        return RT_EOK;
    case PWM_CMD_SET:
    case PWM_CMD_SET_PERIOD:
    case PWM_CMD_SET_PULSE:
    {
        if (cmd != PWM_CMD_SET && !(obj->configured & ch)) return -RT_EIO;
        rt_uint32_t period = cmd == PWM_CMD_SET_PULSE ? obj->period : cfg->period;
        rt_uint32_t pulse = cmd == PWM_CMD_SET_PERIOD ? obj->pulse[index] : cfg->pulse;
        rt_uint32_t hz = lp ? 6500000u : get_sysclk_nhz();
        if (!period || pulse > period || !hz) return -RT_EINVAL;
        rt_uint64_t ticks = ((rt_uint64_t)period * hz + 500000000u) / 1000000000u;
        if (ticks < 2 || ticks > 65536u) return -RT_EINVAL;
        /* Do not silently change another configured channel's shared period. */
        for (unsigned i = 0; i < sizeof(ab32_pwm_obj) / sizeof(ab32_pwm_obj[0]); i++)
        {
            struct ab32_pwm *other = &ab32_pwm_obj[i];
            if (other->pwm_handle == regs &&
                (other->configured & (other == obj ? ~ch : 0xffu)) &&
                other->period != period) return -RT_EBUSY;
        }
        rt_uint32_t duty;
        if (lp)
        {
            /* LPWM duty is high-level ratio DUT/PR; preserve packed neighbor. */
            duty = ((rt_uint64_t)pulse * (ticks - 1) + period / 2) / period;
            unsigned reg = output < 2 ? PWMxxDUT : PWMyyDUT;
            unsigned shift = (output & 1) * 16;
            regs[PWMxPR] = ticks - 1;
            regs[reg] = (regs[reg] & ~(0xfffful << shift)) | ((rt_uint32_t)duty << shift);
        }
        else
        {
            /* Timer DUT encodes LOW time minus one, RT PWM pulse is HIGH time.
             * Exact DC endpoints cannot be promised by this timer encoding;
             * callers needing 0/100% must switch the pin back to GPIO. */
            rt_uint64_t high = ((rt_uint64_t)pulse * hz + 500000000u) / 1000000000u;
            if (!high || high >= ticks) return -RT_EINVAL;
            duty = ticks - high - 1;
            if (obj->period != period)
            {
                regs[TMRxCON] &= ~BIT(0);
                regs[TMRxCNT] = 0;
            }
            regs[TMRxPR] = ticks - 1;
            regs[TMRxDUTY0 + output] = duty;
            if (obj->enabled) regs[TMRxCON] |= BIT(0);
        }
        obj->period = period;
        obj->pulse[index] = pulse;
        obj->configured |= ch;
        return RT_EOK;
    }
    default:
        return -RT_ENOSYS;
    }
}
static rt_err_t drv_pwm_control(struct rt_device_pwm *device, int cmd, void *arg)
{
    if (!device || !arg || !device->parent.user_data) return -RT_EINVAL;
    if (!pwm_ready) return -RT_EIO;
    if (rt_interrupt_get_nest() || !rt_thread_self()) return -RT_EBUSY;
    rt_err_t rc = rt_mutex_take(&pwm_lock, RT_WAITING_FOREVER);
    if (rc) return rc;
    rc = pwm_control_locked(device->parent.user_data, cmd, arg);
    rt_mutex_release(&pwm_lock);
    return rc;
}
static void ab32_hw_pwm_init(struct ab32_pwm *device)
{
    hal_sfr_t regs = device->pwm_handle;
    if (device->name[0] == 'l')
    {
        regs[PWMxCON] = 0; /* shared block, all devices still disabled at boot */
    }
    else
    {
        if (regs == TIM3_BASE) hal_rcu_periph_clk_enable(RCU_TMR3);
        else if (regs == TIM4_BASE) hal_rcu_periph_clk_enable(RCU_TMR4);
        else if (regs == TIM5_BASE) hal_rcu_periph_clk_enable(RCU_TMR5);
        regs[TMRxCON] = 0; /* system clock, no capture/interrupt; stopped */
        regs[TMRxCPND] = BIT(16) | BIT(17);
        regs[TMRxCNT] = 0;
    }
}

static void pwm_get_channel(void)
{
#ifdef BSP_USING_T3_PWM0
    ab32_pwm_obj[T3_PWM_INDEX].channel |= 1 << 0;
#endif
#ifdef BSP_USING_T3_PWM1
    ab32_pwm_obj[T3_PWM_INDEX].channel |= 1 << 1;
#endif
#ifdef BSP_USING_T3_PWM2
    ab32_pwm_obj[T3_PWM_INDEX].channel |= 1 << 2;
#endif
#ifdef BSP_USING_T4_PWM0
    ab32_pwm_obj[T4_PWM_INDEX].channel |= 1 << 0;
#endif
#ifdef BSP_USING_T4_PWM1
    ab32_pwm_obj[T4_PWM_INDEX].channel |= 1 << 1;
#endif
#ifdef BSP_USING_T4_PWM2
    ab32_pwm_obj[T4_PWM_INDEX].channel |= 1 << 2;
#endif
#ifdef BSP_USING_T5_PWM0
    ab32_pwm_obj[T5_PWM_INDEX].channel |= 1 << 0;
#endif
#ifdef BSP_USING_T5_PWM1
    ab32_pwm_obj[T5_PWM_INDEX].channel |= 1 << 1;
#endif
#ifdef BSP_USING_T5_PWM2
    ab32_pwm_obj[T5_PWM_INDEX].channel |= 1 << 2;
#endif
#ifdef BSP_USING_LPWM0
    ab32_pwm_obj[LPWM0_INDEX].channel |= 1 << 0;
#endif
#ifdef BSP_USING_LPWM1
    ab32_pwm_obj[LPWM1_INDEX].channel |= 1 << 0;
#endif
#ifdef BSP_USING_LPWM2
    ab32_pwm_obj[LPWM2_INDEX].channel |= 1 << 0;
#endif
#ifdef BSP_USING_LPWM3
    ab32_pwm_obj[LPWM3_INDEX].channel |= 1 << 0;
#endif
}

static struct rt_pwm_ops drv_ops =
{
    drv_pwm_control
};

static int ab32_pwm_init(void)
{
    rt_err_t rc = rt_mutex_init(&pwm_lock, "pwm", RT_IPC_FLAG_PRIO);
    if (rc) return rc;
    pwm_get_channel();
    for (unsigned i = 0; i < sizeof(ab32_pwm_obj) / sizeof(ab32_pwm_obj[0]); i++)
        ab32_hw_pwm_init(&ab32_pwm_obj[i]);
    hal_pwm_mspinit();
    for (unsigned i = 0; i < sizeof(ab32_pwm_obj) / sizeof(ab32_pwm_obj[0]); i++)
    {
        rc = rt_device_pwm_register(&ab32_pwm_obj[i].pwm_device,
            ab32_pwm_obj[i].name, &drv_ops, &ab32_pwm_obj[i]);
        if (rc) return rc; /* callbacks remain unavailable on partial failure */
    }
    pwm_ready = RT_TRUE;
    return RT_EOK;
}
INIT_DEVICE_EXPORT(ab32_pwm_init);
#endif /* RT_USING_PWM */
