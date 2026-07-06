/*
 * AB32VG1 Startup File for RISC-V GCC
 */

    .section .init
    .globl _start
    .type _start, @function

_start:
    /* 关闭全局中断 (在初始化完成前) */
    /* Skip disabling interrupts for RV32IMAC without Zicsr extension */
    /* We'll handle interrupts in C code after proper initialization */

    /* 1. 初始化全局指针 (Global Pointer) 
       __global_pointer$ 在链接脚本(.ld)中定义 */
.option push
.option norelax
    la gp, __global_pointer$
.option pop

    /* 2. 初始化栈指针 (Stack Pointer)
       _stack_end 在链接脚本中定义 (通常是 RAM 的末尾) */
    la sp, _stack_top

    /* 3. 设置中断向量表地址 (Trap Vector Base Address)
       将 mtvec 寄存器指向 trap_entry 标签 */
    la t0, trap_entry
    /* Skip setting mtvec for RV32IMAC without Zicsr extension */
    /* Interrupt handling will be done differently */

    /* 4. 数据段初始化 (Data Section: Load LMA -> VMA) 
       如果代码在 Flash 运行，需要将已初始化的变量复制到 RAM */
    la a0, _sdata           /* RAM 中数据段起始地址 */
    la a1, _edata           /* RAM 中数据段结束地址 */
    la a2, _sidata          /* Flash 中数据段源地址 */
    bge a0, a1, init_bss    /* 如果段大小为0，跳过 */

copy_data_loop:
    lw t0, 0(a2)            /* 从 Flash 读取 */
    sw t0, 0(a0)            /* 写入 RAM */
    addi a0, a0, 4
    addi a2, a2, 4
    blt a0, a1, copy_data_loop

    /* 5. 清零 BSS 段 (Zero BSS Section) 
       将未初始化的全局变量区域清零 */
init_bss:
    la a0, _sbss            /* BSS 起始地址 */
    la a1, _ebss            /* BSS 结束地址 */
    bge a0, a1, call_system_init

clear_bss_loop:
    sw zero, 0(a0)
    addi a0, a0, 4
    blt a0, a1, clear_bss_loop

    /* 6. 调用系统初始化函数 (C语言)
       通常用于配置时钟、外设等，在进入 main 之前 */
call_system_init:
    call SystemInit

    /* 7. 跳转到裸机主函数 */
    call main

    /* 如果 main 返回，进入死循环 (Safety Net) */
loop_forever:
    j loop_forever

/* ------------------------------------------------------------------------- */
/* 默认的中断/异常入口 (Trap Entry)                                           */
/* ------------------------------------------------------------------------- */
    .align 2
    .globl trap_entry
    .type trap_entry, @function
trap_entry:
    /* 保存上下文 (Context Save) - 这里是一个简化的示例
       实际应用中，RTOS 会接管这里进行任务切换 */
    addi sp, sp, -32
    sw ra, 0(sp)
    sw t0, 4(sp)
    sw t1, 8(sp)
    sw a0, 12(sp)
    
    /* 调用 C 语言的中断分发处理函数 */
    call interrupt_handler_c

    /* 恢复上下文 (Context Restore) */
    lw a0, 12(sp)
    lw t1, 8(sp)
    lw t0, 4(sp)
    lw ra, 0(sp)
    addi sp, sp, 32
    /* Return from trap - using basic jump */
    jalr x0, ra, 0

/* ------------------------------------------------------------------------- */
/* 弱定义函数，防止链接错误                                                   */
/* ------------------------------------------------------------------------- */
    .weak SystemInit
SystemInit:
    ret

    .weak interrupt_handler_c
interrupt_handler_c:
    ret
    
    .weak main
main:
    j loop_forever
