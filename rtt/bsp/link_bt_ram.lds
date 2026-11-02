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
        *src*object.o (.text* .rodata*)
        *src*thread.o (.text* .rodata*)
        *src*timer.o (.text* .rodata*)
        *src*scheduler*.o (.text* .rodata*)
        *src*clock.o (.text* .rodata*)
        *src*kservice.o (.text* .rodata*)
        *kernel*src*mem.o (.text* .rodata*)
        *src*klibc*.o (.text* .rodata*)
        *src*device.o (.text* .rodata*)
        *src*idle.o (.text* .rodata*)
        *bt_hct*_recovered.o (.text* .rodata*)
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
        /* Host mbuf queue operations mask IRQs and stay in SRAM below. */
        EXCLUDE_FILE(*ble_hs_mqueue.o) *nimble*host*.o (.text* .rodata* .srodata*)
        *nimble*transport*.o (.text* .rodata* .srodata*)
        *app_nimble.o (.text* .rodata* .srodata*)
        *nimble_port.o (.text* .rodata* .srodata*)
        *os_msys.o (.text* .rodata* .srodata*)
        *mem.o (.text.mem_malloc* .text.mem_init*)
        /* USB is polled exclusively in thread context; no USB IRQ calls Flash. */
        *usb_dc_ab32.o (.text* .rodata* .srodata*)
        *app_usb_cdc.o (.text* .rodata* .srodata*)
        *usbd_core.o (.text* .rodata* .srodata*)
        *usbd_cdc_acm.o (.text* .rodata* .srodata*)
        /* TF is synchronous thread-only I/O, with polling that yields. */
        *drv_tf.o (.text* .rodata* .srodata*)
        *drv_sdcard.o (.text* .rodata* .srodata*)
        *components*dfs*.o (.text* .rodata* .srodata*)
        /* Linked only by FinSH file/script handling in this configuration. */
        *lib_a-strstr.o (.text* .rodata* .srodata*)
        /* Only thread entry points here; .irq.irrx* (including pin addresses)
         * and every IRQ-masked helper are collected into SRAM below. */
        *drv_irrx.o (.text* .rodata* .srodata*)
        /* RTC is thread-only; its IRQ-masked bridge helpers use .irq.rtc. */
        *drv_rtc.o (.text* .rodata* .srodata*)
        *dev_rtc.o (.text* .rodata* .srodata*)
        /* Calendar conversion / libc RTC access runs in thread context only. */
        *ctime.o (.text* .rodata* .srodata*)
        *board_keys.o (.text* .rodata* .srodata*)
        /* PWM/envelope operations take a mutex and only run in threads. */
        *board_led.o (.text* .rodata* .srodata*)
        *app*main.o (.text* .rodata* .srodata*)
        *drv_pwm.o (.text* .rodata* .srodata*)
        *rt_drv_pwm.o (.text* .rodata* .srodata*)
        *drv_gpio.o (.text.ab32_pin_get .text.ab32_pin_attach_irq
                     .text.ab32_pin_dettach_irq .text.ab32_pin_irq_enable
                     .text.ab32_pin_irq_thread .text.gpio_dispatch .text.ab32_pin_irq_init)
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
    /* Native IR adds SRAM IRQ/critical helpers and a static queue, no thread.
     * Reserve its 1 KiB feature allowance in each optional build budget.
     * Optional GPIO IRQ adds per-pin state and critical helpers (2 KiB).
     * Its lazy worker is a separate runtime allocation from this heap. */
    ASSERT(__heap_end - __heap_start >= (DEFINED(drv_sdcard_init) ? 23K : (DEFINED(ab32_usb_poll) ? 27K : (DEFINED(ble_hs_init) ? 28K : 39K))) - (DEFINED(ab32_irrx_init) ? 1K : 0) - (DEFINED(ab32_pin_irq_handler) ? 2K : 0), "Unified image heap below budget")
    /DISCARD/ : { *(.comment .eh_frame*) }
}
