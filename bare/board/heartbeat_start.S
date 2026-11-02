/* AB32VG1 vendor-header application entry. The header owns ROM download entry;
 * do not clear the CPU boot watchdog register at 0x508 here.
 */
    .section .reset,"ax"
    .global _start
_start:
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
    li tp, 0x84800
    la sp, __stack_top
    la a0, __ram_start
    la a1, __ram_load
    la a2, __ram_size
    li t0, 0x84044       /* vendor ROM memcpy, also used by BSP startup.S */
    jalr ra, t0, 0
    la t0, ram_start
    jr t0

    .section .text.ram_start,"ax"
ram_start:
    li t0, 1
    sw t0, 0x530(zero)  /* PICCONCLR: polling firmware, no interrupts */
    la a0, __bss_start
    la a1, __bss_end
1:
    bgeu a0, a1, 2f
    sw zero, 0(a0)
    addi a0, a0, 4
    j 1b
2:
    call board_init
    call main
3:
    j 3b
