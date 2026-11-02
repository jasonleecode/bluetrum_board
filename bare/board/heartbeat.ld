/* Vendor header maps the application at 0x10000000. Only the entry stub
 * executes there; the heartbeat runs entirely in real SRAM, without a cache ISR.
 * RAM0 ends at 0x30000 (see the vendor BSP link.lds).
 */
ENTRY(_start)
MEMORY
{
    entry (rx) : ORIGIN = 0x10000000, LENGTH = 512
    flash (rx) : ORIGIN = 0x10000200, LENGTH = 256K
    ram (rwx)  : ORIGIN = 0x20000, LENGTH = 64K
}
SECTIONS
{
    .init : { KEEP(*(.reset)) } > entry
    .ram : ALIGN(4)
    {
        __ram_start = .;
        *(.text .text.* .com_text .com_text.*)
        *(.rodata .rodata.* .srodata .srodata.*)
        *(.data .data.* .sdata .sdata.*)
        . = ALIGN(4);
        __ram_end = .;
    } > ram AT > flash
    __ram_load = LOADADDR(.ram);
    __ram_size = SIZEOF(.ram);
    __global_pointer$ = __ram_start + 0x800;
    .bss (NOLOAD) : ALIGN(4)
    {
        __bss_start = .;
        *(.bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > ram
    __stack_top = ORIGIN(ram) + LENGTH(ram);
    ASSERT(__bss_end <= __stack_top - 4096, "heartbeat overlaps stack")
    /DISCARD/ : { *(.comment) *(.eh_frame*) }
}
