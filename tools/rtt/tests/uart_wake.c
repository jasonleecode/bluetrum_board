/* Wake coalescing must retain every UART and IRQs arriving during a drain. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../../rtt/bsp/libraries/hal_drivers/drv_common.c"
static int locked, wakes, calls[3], inject;
void rt_assert_handler(const char *e, const char *f, rt_size_t l) { abort(); }
rt_base_t rt_hw_interrupt_disable(void) { assert(!locked); locked = 1; return 0; }
void rt_hw_interrupt_enable(rt_base_t level) { assert(locked); locked = 0; }
rt_err_t rt_sem_release(rt_sem_t s) { assert(locked && s == &drv_wake); wakes++; return RT_EOK; }
void uart0_irq_process(void) { calls[0]++; if (inject) uart0_irq_post(); }
void uart1_irq_process(void) { calls[1]++; }
void uart2_irq_process(void) { calls[2]++; }
int main(void)
{
    for (int i = 0; i < 1000; ++i) uart0_irq_post();
    uart1_irq_post(); uart2_irq_post();
    assert(wakes == 1 && drv_pending == 7);
    inject = 1;
    drv_process_events(drv_take_pending());
    assert(calls[0] == 1 && calls[1] == 1 && calls[2] == 1);
    assert(wakes == 2 && drv_pending == 1 && !locked);
    inject = 0;
    drv_process_events(drv_take_pending());
    assert(calls[0] == 2 && !drv_pending && !locked);
    puts("PASS: UART coalesced wake, all ports, IRQ during drain retained");
}
