#include "drv_common.h"

#define DRV_THREAD_PRIORITY         18
#define DRV_THREAD_STACK_SIZE       512
#define DRV_THREAD_TIMESLICE        5

enum {
    MSG_UART0_IRQ,
    MSG_UART1_IRQ,
    MSG_UART2_IRQ,
};

static rt_thread_t drv_tid = RT_NULL;
/* UART bytes live in each UART's RX ring. Notifications only mean "drain
 * this ring", so repeated IRQs may share one event bit without losing data.
 * Bits are cleared atomically before processing: an IRQ during the
 * drain remains pending for the next iteration.
 */
static struct rt_semaphore drv_wake;
static rt_uint32_t drv_pending;

rt_section(".irq.uart")
static void drv_notify(rt_uint32_t bit)
{
    rt_base_t level = rt_hw_interrupt_disable();
    rt_bool_t wake = drv_pending == 0;
    drv_pending |= bit;
    if (wake) rt_sem_release(&drv_wake);
    rt_hw_interrupt_enable(level);
}

static rt_uint32_t drv_take_pending(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    rt_uint32_t events = drv_pending;
    drv_pending = 0;
    rt_hw_interrupt_enable(level);
    return events;
}

void uart0_irq_process(void);
void uart1_irq_process(void);
void uart2_irq_process(void);

rt_section(".irq.uart")
void uart0_irq_post(void)
{
    drv_notify(1u << MSG_UART0_IRQ);
}

rt_section(".irq.uart")
void uart1_irq_post(void)
{
    drv_notify(1u << MSG_UART1_IRQ);
}

rt_section(".irq.uart")
void uart2_irq_post(void)
{
    drv_notify(1u << MSG_UART2_IRQ);
}

static void drv_process_events(rt_uint32_t events)
{
#ifdef BSP_USING_UART0
    if (events & (1u << MSG_UART0_IRQ)) uart0_irq_process();
#endif
#ifdef BSP_USING_UART1
    if (events & (1u << MSG_UART1_IRQ)) uart1_irq_process();
#endif
#ifdef BSP_USING_UART2
    if (events & (1u << MSG_UART2_IRQ)) uart2_irq_process();
#endif
}

static void drv_thread_entry(void *parameter)
{
    while (1) {
        if (rt_sem_take(&drv_wake, RT_WAITING_FOREVER) == RT_EOK)
            drv_process_events(drv_take_pending());
    }
}

static int drv_thread_init(void)
{
    rt_err_t ret = rt_sem_init(&drv_wake, "drv", 0, RT_IPC_FLAG_FIFO);
    if (ret != RT_EOK)
        return ret;
    drv_tid = rt_thread_create("drv",
            drv_thread_entry,
            RT_NULL,
            DRV_THREAD_STACK_SIZE,
            DRV_THREAD_PRIORITY,
            DRV_THREAD_TIMESLICE);

    if (drv_tid == RT_NULL)
    {
        rt_sem_detach(&drv_wake);
        return -RT_ENOMEM;
    }
    return rt_thread_startup(drv_tid);
}
INIT_PREV_EXPORT(drv_thread_init);
