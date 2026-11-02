/* Actual NPL with modeled RTT IRQ/IPC/time. No radio or mocked Host success. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../rtt/bsp/board/ports/nimble/npl_rtthread.c"
static rt_tick_t tick;
static int masked, callbacks;
void rt_assert_handler(const char *expr, const char *fn, rt_size_t line)
{ fprintf(stderr, "%s:%lu: %s\n", fn, (unsigned long)line, expr); abort(); }
rt_base_t rt_hw_interrupt_disable(void) { int old = masked; masked = 1; return !old; }
void rt_hw_interrupt_enable(rt_base_t level) { if (level) masked = 0; }
rt_tick_t rt_tick_get(void) { return tick; }
rt_err_t rt_sem_init(rt_sem_t s, const char *name, rt_uint32_t n, rt_uint8_t flag)
{ assert(strlen(name) < RT_NAME_MAX); s->value = n; return RT_EOK; }
rt_err_t rt_sem_release(rt_sem_t s) { assert(s->value < 65535); ++s->value; return RT_EOK; }
rt_err_t rt_sem_take(rt_sem_t s, rt_int32_t t)
{ if (s->value) { --s->value; return RT_EOK; } assert(t != RT_WAITING_FOREVER); tick += t; return -RT_ETIMEOUT; }
void rt_timer_init(rt_timer_t t, const char *name, rt_timer_func_t fn, void *arg, rt_tick_t time, rt_uint8_t flags)
{ assert(strlen(name) < RT_NAME_MAX); t->timeout_func = fn; t->parameter = arg; t->parent.flag = flags; t->init_tick = time; }
rt_err_t rt_timer_stop(rt_timer_t t) { t->parent.flag &= ~RT_TIMER_FLAG_ACTIVATED; return RT_EOK; }
rt_err_t rt_timer_start(rt_timer_t t) { t->parent.flag |= RT_TIMER_FLAG_ACTIVATED; return RT_EOK; }
rt_err_t rt_timer_control(rt_timer_t t, int cmd, void *arg)
{ assert(cmd == RT_TIMER_CTRL_SET_TIME); t->init_tick = *(ble_npl_time_t *)arg; return RT_EOK; }
static void callback(struct ble_npl_event *ev) { assert(ble_npl_event_get_arg(ev) == &callbacks); ++callbacks; }
int main(void)
{
    struct ble_npl_eventq q;
    struct ble_npl_event events[40];
    ble_npl_eventq_init(&q);
    for (int i = 0; i < 40; ++i) {
        ble_npl_event_init(&events[i], callback, &callbacks);
        ble_npl_eventq_put(&q, &events[i]);
        ble_npl_eventq_put(&q, &events[i]); /* duplicate suppressed */
    }
    ble_npl_eventq_remove(&q, &events[17]);
    for (int i = 0; i < 40; ++i) {
        if (i == 17) continue;
        assert(ble_npl_eventq_get(&q, 0) == &events[i]);
        assert(!ble_npl_event_is_queued(&events[i]));
        ble_npl_event_run(&events[i]);
    }
    assert(callbacks == 39 && ble_npl_eventq_is_empty(&q));
    tick = 100; assert(!ble_npl_eventq_get(&q, 10) && tick == 110);
    ble_npl_eventq_put(&q, &events[0]); ble_npl_eventq_remove(&q, &events[0]);
    assert(!ble_npl_eventq_get(&q, 10) && tick == 120); /* stale wake */
    struct ble_npl_callout co;
    ble_npl_callout_init(&co, &q, callback, &callbacks);
    assert(co.timer.parent.flag & RT_TIMER_FLAG_SOFT_TIMER);
    assert(ble_npl_callout_reset(&co, 0) == BLE_NPL_OK && co.timer.init_tick == 1);
    assert(ble_npl_callout_reset(&co, UINT32_MAX) == BLE_NPL_EINVAL);
    co.timer.timeout_func(co.timer.parameter);
    assert(ble_npl_event_is_queued(&co.ev));
    ble_npl_callout_stop(&co);
    assert(!ble_npl_event_is_queued(&co.ev) && !ble_npl_callout_is_active(&co));
    tick = UINT32_MAX - 3;
    assert(!ble_npl_callout_reset(&co, 8));
    assert(ble_npl_callout_remaining_ticks(&co, UINT32_MAX - 1) == 6);
    ble_npl_time_t ticks;
    assert(!ble_npl_time_ms_to_ticks(1234, &ticks) && ticks == 1234);
    assert(ble_npl_time_ms_to_ticks(UINT32_MAX, &ticks) == BLE_NPL_EINVAL);
    assert(!masked && !critical_depth);
    puts("PASS: NPL FIFO/duplicate/remove/wakeup/timeout, soft callouts, tick wrap, object names");
}
