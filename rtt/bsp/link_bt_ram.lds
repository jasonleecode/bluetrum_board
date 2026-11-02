/* Unified RTT/FM/BT: reserve controller ABI RAM; use both executable SRAM banks.
 * Shell and PCM framework run through the flash cache in thread context.
 * Kernel, cache refill, controller, HAL and interrupt paths stay in SRAM.
 * No code or RT heap may overlap 0x11000..0x13800 or ROM 0x10000..0x11000. */
ENTRY(_start)
MEMORY
{
    init (rx) : ORIGIN = 0x10000000, LENGTH = 512
    flash (rx) : ORIGIN = 0x10000200, LENGTH = 1020K
    bram (rw) : ORIGIN = 0x11000, LENGTH = 10K
    ram (rwx) : ORIGIN = 0x13800, LENGTH = 110K
    stack (rw) : ORIGIN = 0x2f000, LENGTH = 4K
    ram1 (rwx) : ORIGIN = 0x50000, LENGTH = 0x7a00
}
SECTIONS
{
    .init : { KEEP(*(.reset)) } > init
    .ram1 : ALIGN(4)
    {
        __ram1_vma = .;
        *src*ipc.o (.text* .rodata*)
        *src*irq.o (.text* .rodata*)
        *src*object.o (.text* .rodata*)
        *src*thread.o (.text* .rodata*)
        *src*timer.o (.text* .rodata*)
        *src*scheduler*.o (.text* .rodata*)
        *src*clock.o (.text* .rodata*)
        *src*kservice.o (.text* .rodata*)
        *src*mem.o (.text* .rodata*)
        *src*klibc*.o (.text* .rodata*)
        *src*device.o (.text* .rodata*)
        *src*idle.o (.text* .rodata*)
        *src*components.o (.text* .rodata*)
        *bt_hct*_recovered.o (.text* .rodata*)
        *bt_complex_recovered.o (.text* .rodata*)
        . = ALIGN(4);
    } > ram1 AT > flash
    __ram1_lma = LOADADDR(.ram1);
    __ram1_size = SIZEOF(.ram1);
    .flash : ALIGN(512)
    {
        /* This BSP defers UART RX notifications to drv_thread_entry; FinSH
         * callbacks therefore run in thread context. sound0 is playback-only
         * and its framework completion also runs in the audio worker. A new
         * IRQ-driven consumer must first move its callback/callees to SRAM.
         */
        *components*finsh*.o (.text* .rodata* .srodata*)
        *dev_audio.o (.text* .rodata* .srodata*)
        *dev_audio_pipe.o (.text* .rodata* .srodata*)
        *drv_sound.o (.text.pcmcheck)
        . = ALIGN(512);
    } > flash
    __bank_size = SIZEOF(.flash);
    .comm : ALIGN(512)
    {
        __comm_vma = .;
        KEEP(*(.vector))
        *(.irq* .com_text* .com_rodata* .sleep_text*)
        . = ALIGN(4);
        __rt_init_start = .;
        KEEP(*(SORT(.rti_fn*)))
        __rt_init_end = .;
        __ctors_start__ = .;
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        __ctors_end__ = .;
        __rtatcmdtab_start = .;
        KEEP(*(RtAtCmdTab))
        __rtatcmdtab_end = .;
        __fsymtab_start = .;
        KEEP(*(FSymTab))
        __fsymtab_end = .;
        __vsymtab_start = .;
        KEEP(*(VSymTab))
        __vsymtab_end = .;
        *(.text .text.*)
        *(.rodata .rodata.* .srodata .srodata.*)
        *(.data .data.* .sdata .sdata.*)
        . = ALIGN(4);
    } > ram AT > flash
    __comm_lma = LOADADDR(.comm);
    __comm_size = SIZEOF(.comm);
    .bram (NOLOAD) : ALIGN(4)
    {
        __bt_start = .;
        KEEP(*(.btmem.bthw))
        *(.btmem*)
        . = ALIGN(4);
        __bt_end = .;
    } > bram
    .bss (NOLOAD) : ALIGN(16)
    {
        __bss_start = .;
        *(.bss .bss.* .sbss .sbss.* COMMON)
        *(.buf*)
        . = ALIGN(16);
        __bss_end = .;
    } > ram
    __bss_size = __bss_end - __bss_start;
    .heap (NOLOAD) :
    {
        __heap_start = .;
        . = ORIGIN(ram) + LENGTH(ram);
        __heap_end = .;
    } > ram
    .stack (NOLOAD) :
    {
        __irq_stack_start = .;
        . += LENGTH(stack);
        __irq_stack = .;
    } > stack
    __irq_stack_size = SIZEOF(.stack);
    ASSERT(bb_get_without_vehicle == 0x11000, "BT fixed RAM base is incorrect")
    ASSERT(__bt_end <= 0x13800, "BT controller RAM exceeds reservation")
    ASSERT(__heap_end - __heap_start >= 40K, "Unified BT image leaves less than 40 KiB heap; review NimBLE budget")
    /DISCARD/ : { *(.comment .eh_frame*) }
}
