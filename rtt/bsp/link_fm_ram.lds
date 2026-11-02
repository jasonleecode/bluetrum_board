/* FM bring-up: execute code/data in SRAM so cache misses cannot recurse into
 * the scheduler or occur while changing PLL/SPI clocks. The vendor header
 * still loads the entry at 0x10000000; startup.S copies .comm using ROM memcpy.
 * Keep 0x10000..0x11000 for ROM and 4 KiB at the top for the IRQ/startup stack.
 */
ENTRY(_start)
MEMORY
{
    init (rx) : ORIGIN = 0x10000000, LENGTH = 512
    flash (rx) : ORIGIN = 0x10000200, LENGTH = 1020K
    ram (rwx) : ORIGIN = 0x11000, LENGTH = 120K
    stack (rw) : ORIGIN = 0x2f000, LENGTH = 4K
}
SECTIONS
{
    .init : { KEEP(*(.reset)) } > init
    .comm : ALIGN(4)
    {
        __comm_vma = .;
        KEEP(*(.vector))
        *(.irq* .com_text*)
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
    /* Retain the common startup's optional second copy, with zero length. */
    __ram1_vma = 0x50000;
    __ram1_lma = __comm_lma;
    __ram1_size = 0;
    __bank_size = 0;
    .bss (NOLOAD) : ALIGN(16)
    {
        __bss_start = .;
        *(.bss .bss.* .sbss .sbss.* COMMON)
        *(.buf* .btmem*)
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
    ASSERT(__heap_end - __heap_start >= 16K, "FM SRAM image leaves less than 16 KiB heap")
    /DISCARD/ : { *(.comment .eh_frame*) }
}
