/* SPDX-License-Identifier: Apache-2.0
 * Single-core RTT NPL. Intrusive queues have no fixed event-count limit.
 * All callouts use soft timers; no Host/flash code is entered from an IRQ.
 */
#include "nimble/nimble_npl.h"
#include <limits.h>
static unsigned critical_depth;
static rt_int32_t timeout_ticks(ble_npl_time_t ticks)
{ return ticks == BLE_NPL_TIME_FOREVER ? RT_WAITING_FOREVER : (ticks > INT32_MAX ? INT32_MAX : ticks); }
static ble_npl_error_t error(rt_err_t rc)
{ return rc == RT_EOK ? BLE_NPL_OK : rc == -RT_ETIMEOUT ? BLE_NPL_TIMEOUT : BLE_NPL_ERROR; }
bool ble_npl_os_started(void) { return rt_thread_self() != RT_NULL; }
void *ble_npl_get_current_task_id(void) { return rt_thread_self(); }
uint32_t ble_npl_hw_enter_critical(void)
{ rt_base_t level = rt_hw_interrupt_disable(); ++critical_depth; return level; }
void ble_npl_hw_exit_critical(uint32_t level)
{ RT_ASSERT(critical_depth); --critical_depth; rt_hw_interrupt_enable(level); }
bool ble_npl_hw_is_in_critical(void) { return critical_depth != 0; }
void ble_npl_event_init(struct ble_npl_event *ev, ble_npl_event_fn *fn, void *arg)
{ rt_list_init(&ev->link); ev->owner = RT_NULL; ev->fn = fn; ev->arg = arg; }
void ble_npl_eventq_init(struct ble_npl_eventq *q)
{ rt_list_init(&q->events); rt_sem_init(&q->wake, "nplevq", 0, RT_IPC_FLAG_PRIO); }
void ble_npl_eventq_put(struct ble_npl_eventq *q, struct ble_npl_event *ev)
{
    uint32_t level = ble_npl_hw_enter_critical();
    bool wake = false;
    if (!ev->owner) {
        wake = rt_list_isempty(&q->events);
        ev->owner = q;
        rt_list_insert_before(&q->events, &ev->link);
    }
    ble_npl_hw_exit_critical(level);
    if (wake) rt_sem_release(&q->wake);
}
struct ble_npl_event *ble_npl_eventq_get(struct ble_npl_eventq *q, ble_npl_time_t timeout)
{
    rt_tick_t start = rt_tick_get();
    rt_int32_t limit = timeout_ticks(timeout);
    for (;;) {
        uint32_t level = ble_npl_hw_enter_critical();
        struct ble_npl_event *ev = RT_NULL;
        if (!rt_list_isempty(&q->events)) {
            ev = rt_list_entry(q->events.next, struct ble_npl_event, link);
            rt_list_remove(&ev->link); ev->owner = RT_NULL;
        }
        ble_npl_hw_exit_critical(level);
        if (ev) { rt_sem_take(&q->wake, 0); return ev; }
        rt_tick_t elapsed = rt_tick_get() - start;
        if (limit != RT_WAITING_FOREVER && elapsed >= (rt_tick_t)limit) return RT_NULL;
        rt_int32_t wait = limit == RT_WAITING_FOREVER ? limit : limit - elapsed;
        if (rt_sem_take(&q->wake, wait) != RT_EOK) return RT_NULL;
    }
}
void ble_npl_eventq_remove(struct ble_npl_eventq *q, struct ble_npl_event *ev)
{
    uint32_t level = ble_npl_hw_enter_critical();
    if (ev->owner == q) { rt_list_remove(&ev->link); ev->owner = RT_NULL; }
    ble_npl_hw_exit_critical(level);
}
bool ble_npl_eventq_is_empty(struct ble_npl_eventq *q)
{
    uint32_t level = ble_npl_hw_enter_critical();
    bool empty = rt_list_isempty(&q->events);
    ble_npl_hw_exit_critical(level); return empty;
}
bool ble_npl_event_is_queued(struct ble_npl_event *ev) { return ev->owner != RT_NULL; }
void *ble_npl_event_get_arg(struct ble_npl_event *ev) { return ev->arg; }
void ble_npl_event_set_arg(struct ble_npl_event *ev, void *arg) { ev->arg = arg; }
void ble_npl_event_run(struct ble_npl_event *ev) { if (ev) ev->fn(ev); }
ble_npl_error_t ble_npl_mutex_init(struct ble_npl_mutex *m)
{ return error(rt_mutex_init(&m->lock, "nplmtx", RT_IPC_FLAG_PRIO)); }
ble_npl_error_t ble_npl_mutex_pend(struct ble_npl_mutex *m, ble_npl_time_t t)
{ return error(rt_mutex_take(&m->lock, timeout_ticks(t))); }
ble_npl_error_t ble_npl_mutex_release(struct ble_npl_mutex *m)
{ return error(rt_mutex_release(&m->lock)); }
ble_npl_error_t ble_npl_sem_init(struct ble_npl_sem *s, uint16_t tokens)
{ return error(rt_sem_init(&s->sem, "nplsem", tokens, RT_IPC_FLAG_PRIO)); }
ble_npl_error_t ble_npl_sem_pend(struct ble_npl_sem *s, ble_npl_time_t t)
{ return error(rt_sem_take(&s->sem, timeout_ticks(t))); }
ble_npl_error_t ble_npl_sem_release(struct ble_npl_sem *s)
{ return error(rt_sem_release(&s->sem)); }
uint16_t ble_npl_sem_get_count(struct ble_npl_sem *s) { return s->sem.value; }
static void callout_fire(void *arg)
{
    struct ble_npl_callout *co = arg;
    if (co->evq) ble_npl_eventq_put(co->evq, &co->ev);
    else ble_npl_event_run(&co->ev);
}
void ble_npl_callout_init(struct ble_npl_callout *co, struct ble_npl_eventq *q,
                         ble_npl_event_fn *fn, void *arg)
{
    co->evq = q; co->expiry = 0;
    ble_npl_event_init(&co->ev, fn, arg);
    rt_timer_init(&co->timer, "npltmr", callout_fire, co, 1,
                  RT_TIMER_FLAG_ONE_SHOT | RT_TIMER_FLAG_SOFT_TIMER);
}
void ble_npl_callout_stop(struct ble_npl_callout *co)
{
    rt_timer_stop(&co->timer);
    if (co->evq) ble_npl_eventq_remove(co->evq, &co->ev);
}
ble_npl_error_t ble_npl_callout_reset(struct ble_npl_callout *co, ble_npl_time_t ticks)
{
    if (ticks > INT32_MAX) return BLE_NPL_EINVAL;
    if (!ticks) ticks = 1;
    ble_npl_callout_stop(co);
    co->expiry = rt_tick_get() + ticks;
    rt_timer_control(&co->timer, RT_TIMER_CTRL_SET_TIME, &ticks);
    return error(rt_timer_start(&co->timer));
}
bool ble_npl_callout_is_active(struct ble_npl_callout *co)
{ return !!(co->timer.parent.flag & RT_TIMER_FLAG_ACTIVATED); }
ble_npl_time_t ble_npl_callout_get_ticks(struct ble_npl_callout *co) { return co->expiry; }
ble_npl_time_t ble_npl_callout_remaining_ticks(struct ble_npl_callout *co, ble_npl_time_t now)
{ int32_t left = co->expiry - now; return ble_npl_callout_is_active(co) && left > 0 ? left : 0; }
void ble_npl_callout_set_arg(struct ble_npl_callout *co, void *arg) { co->ev.arg = arg; }
ble_npl_time_t ble_npl_time_get(void) { return rt_tick_get(); }
ble_npl_error_t ble_npl_time_ms_to_ticks(uint32_t ms, ble_npl_time_t *ticks)
{
#if RT_TICK_PER_SECOND == 1000
    if (ms > INT32_MAX) return BLE_NPL_EINVAL;
    *ticks = ms; return BLE_NPL_OK;
#else
    uint64_t v = ((uint64_t)ms * RT_TICK_PER_SECOND + 999) / 1000;
    if (v > INT32_MAX) return BLE_NPL_EINVAL;
    *ticks = v; return BLE_NPL_OK;
#endif
}
ble_npl_error_t ble_npl_time_ticks_to_ms(ble_npl_time_t ticks, uint32_t *ms)
{
#if RT_TICK_PER_SECOND == 1000
    *ms = ticks; return BLE_NPL_OK;
#else
    uint64_t v = (uint64_t)ticks * 1000 / RT_TICK_PER_SECOND;
    if (v > UINT32_MAX) return BLE_NPL_EINVAL;
    *ms = v; return BLE_NPL_OK;
#endif
}
ble_npl_time_t ble_npl_time_ms_to_ticks32(uint32_t ms)
{ ble_npl_time_t ticks; return ble_npl_time_ms_to_ticks(ms, &ticks) ? INT32_MAX : ticks; }
uint32_t ble_npl_time_ticks_to_ms32(ble_npl_time_t ticks)
{ uint32_t ms; return ble_npl_time_ticks_to_ms(ticks, &ms) ? UINT32_MAX : ms; }
void ble_npl_time_delay(ble_npl_time_t ticks) { rt_thread_delay(timeout_ticks(ticks)); }
