/*
 * Copyright (c) 2020-2021, Bluetrum Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2021-01-22     greedyhao         first version
 */

#include "board.h"
#ifdef BSP_USING_TIM
#include "tim_config.h"

//#define DRV_DEBUG
#define LOG_TAG             "drv.hwtimer"
#include <drv_log.h>

#ifdef RT_USING_HWTIMER

enum
{
#ifdef BSP_USING_TIM1
    TIM1_INDEX,
#endif
#ifdef BSP_USING_TIM2
    TIM2_INDEX,
#endif
#ifdef BSP_USING_TIM3
    TIM3_INDEX,
#endif
#ifdef BSP_USING_TIM4
    TIM4_INDEX,
#endif
#ifdef BSP_USING_TIM5
    TIM5_INDEX,
#endif
};

struct ab32_hwtimer
{
    rt_hwtimer_t time_device;
    hal_sfr_t     tim_handle;
    char *name;
    irq_type tim_irqn;
    struct rt_hwtimer_info info;
};

static struct ab32_hwtimer ab32_hwtimer_obj[] =
{
#ifdef BSP_USING_TIM1
    TIM1_CONFIG,
#endif

#ifdef BSP_USING_TIM2
    TIM2_CONFIG,
#endif

#ifdef BSP_USING_TIM3
    TIM3_CONFIG,
#endif

#ifdef BSP_USING_TIM4
    TIM4_CONFIG,
#endif

#ifdef BSP_USING_TIM5
    TIM5_CONFIG,
#endif
};

rt_section(".irq.timer")
static void _rt_device_hwtimer_isr(rt_hwtimer_t *timer)
{
    RT_ASSERT(timer != RT_NULL);

    timer->overflow ++;

    if (timer->cycles != 0)
    {
        timer->cycles --;
    }

    if (timer->cycles == 0)
    {
        timer->cycles = timer->reload;

        if (timer->mode == HWTIMER_MODE_ONESHOT)
        {
            if (timer->ops->stop != RT_NULL)
            {
                timer->ops->stop(timer);
            }
        }

        if (timer->parent.rx_indicate != RT_NULL)
        {
            timer->parent.rx_indicate(&timer->parent, sizeof(struct rt_hwtimerval));
        }
    }
}

static void timer_init(struct rt_hwtimer_device *timer, rt_uint32_t state)
{
    rt_uint32_t prescaler_value = 0;
    hal_sfr_t tim = RT_NULL;
    struct ab32_hwtimer *tim_device = RT_NULL;

    RT_ASSERT(timer != RT_NULL);
    tim = (hal_sfr_t)timer->parent.user_data;

    if (state)
    {
        tim_device = (struct ab32_hwtimer *)timer;

        if (timer->info->cntmode != HWTIMER_CNTMODE_UP)
        {
            LOG_E("Only support HWTIMER_CNTMODE_UP!");
        }

        if (tim == TIM1_BASE) hal_rcu_periph_clk_enable(RCU_TMR1);
        else if (tim == TIM2_BASE) hal_rcu_periph_clk_enable(RCU_TMR2);
        else if (tim == TIM3_BASE) hal_rcu_periph_clk_enable(RCU_TMR3);
        else if (tim == TIM4_BASE) hal_rcu_periph_clk_enable(RCU_TMR4);
        else if (tim == TIM5_BASE) hal_rcu_periph_clk_enable(RCU_TMR5);

        /* set tim int */
        tim[TMRxCON] = BIT(7);

        LOG_D("%s init success", tim_device->name);
    } else {
        /* stop timer */
        tim[TMRxCON] = 0;
    }
}

static rt_err_t timer_start(rt_hwtimer_t *timer, rt_uint32_t t, rt_hwtimer_mode_t opmode)
{
    hal_sfr_t tim = (hal_sfr_t)timer->parent.user_data;
    rt_uint32_t clock = get_sysclk_nhz();
    rt_uint64_t count;
    if (!t || timer->freq <= 0 || clock % timer->freq ||
        (opmode != HWTIMER_MODE_PERIOD && opmode != HWTIMER_MODE_ONESHOT))
        return -RT_EINVAL;
    count = (rt_uint64_t)t * (clock / timer->freq);
    if (!count || count > 0x100000000ULL)
        return -RT_EINVAL;
    tim[TMRxCON] &= ~BIT(0);
    tim[TMRxCPND] = (tim == TIM1_BASE || tim == TIM2_BASE) ? BIT(9) : BIT(16);
    tim[TMRxCNT] = 0;
    tim[TMRxPR] = (rt_uint32_t)(count - 1);
    tim[TMRxCON] |= BIT(0);
    /* The common ISR stops one-shot timers after the last software cycle. */
    return RT_EOK;
}

rt_section(".irq.timer")
static void timer_stop(rt_hwtimer_t *timer)
{
    hal_sfr_t tim = RT_NULL;

    RT_ASSERT(timer != RT_NULL);

    tim = (hal_sfr_t)timer->parent.user_data;

    /* stop timer */
    tim[TMRxCON] &= ~BIT(0);

    /* set tim cnt */
    tim[TMRxCNT] = 0;
}

static rt_err_t timer_ctrl(rt_hwtimer_t *timer, rt_uint32_t cmd, void *arg)
{
    hal_sfr_t tim = RT_NULL;
    rt_err_t result = RT_EOK;

    RT_ASSERT(timer != RT_NULL);
    RT_ASSERT(arg != RT_NULL);

    tim = (hal_sfr_t)timer->parent.user_data;

    switch (cmd)
    {
    case HWTIMER_CTRL_FREQ_SET:
    {
        rt_uint32_t freq = *(rt_uint32_t *)arg;
        if (!freq || freq > get_sysclk_nhz() || get_sysclk_nhz() % freq)
            return -RT_EINVAL;
    }
    break;
    default:
    {
        result = -RT_ENOSYS;
    }
    break;
    }

    return result;
}

static rt_uint32_t timer_counter_get(rt_hwtimer_t *timer)
{
    hal_sfr_t tim = RT_NULL;

    RT_ASSERT(timer != RT_NULL);

    tim = (hal_sfr_t)timer->parent.user_data;

    return tim[TMRxCNT] / (get_sysclk_nhz() / timer->freq);
}

static const struct rt_hwtimer_info _info = TIM_DEV_INFO_CONFIG;

rt_section(".irq.timer.ops")
static const struct rt_hwtimer_ops _ops =
{
    .init = timer_init,
    .start = timer_start,
    .stop = timer_stop,
    .count_get = timer_counter_get,
    .control = timer_ctrl,
};

#if defined(BSP_USING_TIM2) || defined(BSP_USING_TIM4) || defined(BSP_USING_TIM5)
rt_section(".irq.timer")
void timer2_4_5_isr(int vector, void *param)
{
    rt_interrupt_enter();
#ifdef BSP_USING_TIM2
    if (ab32_hwtimer_obj[TIM2_INDEX].tim_handle[TMRxCON] & BIT(9)) {
        ab32_hwtimer_obj[TIM2_INDEX].tim_handle[TMRxCPND] = BIT(9);
        _rt_device_hwtimer_isr(&ab32_hwtimer_obj[TIM2_INDEX].time_device);
    }
#endif
#ifdef BSP_USING_TIM4
    if (ab32_hwtimer_obj[TIM4_INDEX].tim_handle[TMRxCON] & BIT(16)) {
        ab32_hwtimer_obj[TIM4_INDEX].tim_handle[TMRxCPND] = BIT(16);
        _rt_device_hwtimer_isr(&ab32_hwtimer_obj[TIM4_INDEX].time_device);
    }
#endif
#ifdef BSP_USING_TIM5
    if (ab32_hwtimer_obj[TIM5_INDEX].tim_handle[TMRxCON] & BIT(16)) {
        ab32_hwtimer_obj[TIM5_INDEX].tim_handle[TMRxCPND] = BIT(16);
        _rt_device_hwtimer_isr(&ab32_hwtimer_obj[TIM5_INDEX].time_device);
    }
#endif
    rt_interrupt_leave();
}
#endif

#ifdef BSP_USING_TIM3
rt_section(".irq.timer")
void timer3_isr(int vector, void *param)
{
    rt_interrupt_enter();
    ab32_hwtimer_obj[TIM3_INDEX].tim_handle[TMRxCPND] = BIT(16);
    _rt_device_hwtimer_isr(&ab32_hwtimer_obj[TIM3_INDEX].time_device);
    rt_interrupt_leave();
}
#endif

#ifdef BSP_USING_TIM1
rt_section(".irq.timer")
void timer1_isr(int vector, void *param)
{
    rt_interrupt_enter();
    ab32_hwtimer_obj[TIM1_INDEX].tim_handle[TMRxCPND] = BIT(9);
    _rt_device_hwtimer_isr(&ab32_hwtimer_obj[TIM1_INDEX].time_device);
    rt_interrupt_leave();
}
#endif

static int ab32_hwtimer_init(void)
{
    int i = 0;
    int result = RT_EOK;

    for (i = 0; i < sizeof(ab32_hwtimer_obj) / sizeof(ab32_hwtimer_obj[0]); i++)
    {
        ab32_hwtimer_obj[i].info = _info;
        /* API counts are virtual ticks; reserve room for hardware scaling. */
        ab32_hwtimer_obj[i].info.maxcnt = 0xffffffffu /
            (get_sysclk_nhz() / _info.minfreq + 1);
        ab32_hwtimer_obj[i].time_device.info = &ab32_hwtimer_obj[i].info;
        ab32_hwtimer_obj[i].time_device.ops  = &_ops;
        if (rt_device_hwtimer_register(&ab32_hwtimer_obj[i].time_device, ab32_hwtimer_obj[i].name, (void *)ab32_hwtimer_obj[i].tim_handle) == RT_EOK)
        {
            LOG_D("%s register success", ab32_hwtimer_obj[i].name);
        }
        else
        {
            LOG_E("%s register failed", ab32_hwtimer_obj[i].name);
            result = -RT_ERROR;
        }
    }

#ifdef BSP_USING_TIM1
    rt_hw_interrupt_install(IRQ_TMR1_VECTOR, timer1_isr, RT_NULL, "t1_isr");
#endif
#if defined(BSP_USING_TIM2) || defined(BSP_USING_TIM4) || defined(BSP_USING_TIM5)
    rt_hw_interrupt_install(IRQ_TMR2_4_5_VECTOR, timer2_4_5_isr, RT_NULL, "t245_isr");
#endif
#ifdef BSP_USING_TIM3
    rt_hw_interrupt_install(IRQ_IRRX_VECTOR, timer3_isr, RT_NULL, "t3_isr");
#endif

    return result;
}
INIT_BOARD_EXPORT(ab32_hwtimer_init);

#endif /* RT_USING_HWTIMER */
#endif /* BSP_USING_TIM */
