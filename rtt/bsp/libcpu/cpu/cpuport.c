/*
 * Copyright (c) 2020-2020, Bluetrum Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2020/11/18     greedyhao    Bluetrum RISC-V porting code.
 */

#include <rthw.h>
#include <rtthread.h>

volatile rt_ubase_t  rt_interrupt_from_thread = 0;
volatile rt_ubase_t  rt_interrupt_to_thread   = 0;
volatile rt_uint32_t rt_thread_switch_interrupt_flag = 0;
rt_uint32_t rt_cur_thread_sp = 0;

/**
 * This function will initialize thread stack
 *
 * @param tentry the entry of thread
 * @param parameter the parameter of entry
 * @param stack_addr the beginning stack address
 * @param texit the function will be called when thread exit
 *
 * @return stack address
 */
rt_uint8_t *rt_hw_stack_init(void       *tentry,
                             void       *parameter,
                             rt_uint8_t *stack_addr,
                             void       *texit)
{
    rt_uint32_t *stk;
    rt_uint32_t tp;
    __asm__ volatile ("mv %0, tp" : "=r" (tp));

    stack_addr += sizeof(rt_uint32_t);
    stack_addr = (rt_uint8_t *)RT_ALIGN_DOWN((rt_ubase_t)stack_addr, 16);
    /* Match context_gcc.S, including one word of ABI alignment padding. */
    stk = (rt_uint32_t *)stack_addr - 32;
    rt_memset(stk, 0, 32 * sizeof(*stk));
    stk[0] = (rt_uint32_t)texit;
    stk[1] = tp;
    stk[7] = (rt_uint32_t)parameter;
    stk[29] = (rt_uint32_t)tentry;
    stk[30] = 0x10003;                 /* EPICCON */

    /* return task's current stack address */
    return (rt_uint8_t *)stk;
}
