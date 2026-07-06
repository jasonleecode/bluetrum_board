/* AB32VG1 Linker Script for RISC-V */

MEMORY
{
    FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 4M /* ROM/Flash */
    RAM  (xrw) : ORIGIN = 0x20000000, LENGTH = 192K /* SRAM */
}

ENTRY(_start)  /* Define entry point */

SECTIONS
{
    /* Reset and interrupt vectors */
    .init : {
        *(.init)
    } > FLASH

    /* Program code */
    .text : {
        *(.text)
        *(.text.*)
    } > FLASH

    /* RO data */
    .rodata : {
        *(.rodata)
        *(.rodata.*)
    } > FLASH

    /* Data section in RAM but stored in Flash */
    .data : ALIGN(4)
    {
        _sdata = .;        /* Global symbol at start of .data */
        *(.data)
        *(.data.*)
        _edata = .;        /* Global symbol at end of .data */
    } > RAM AT > FLASH

    _sidata = LOADADDR(.data); /* 获取在 Flash 中的加载地址 */

    /* BSS section in RAM */
    .bss (NOLOAD) : ALIGN(4)
    {
        _sbss = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        _ebss = .;
    } > RAM

    /* Stack top */
    _stack_top = ORIGIN(RAM) + LENGTH(RAM);
    _stack_bottom = ORIGIN(RAM) + LENGTH(RAM) - 4K; /* Reserve 4KB for stack */
    
    /* Define global pointer */
    . = ALIGN(4);
    __global_pointer$ = . + 0x800;

    /* End of used memory */
    _end = .;
}