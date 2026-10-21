/* Recovered from libbtctrl.a:baseband_platform.o, not a complete BT port.
 * These strong definitions replace three weak vendor implementations. Other
 * vendor hooks remain available, including their nonfunctional defaults. */
#include "bt_platform_recovered.h"

#define BT_REG(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define BT_SECTION(name) __attribute__((section(name)))

typedef void (*bt_isr_t)(void);
extern bt_isr_t register_isr(int vector, bt_isr_t handler);
extern uint32_t bt_get_packet(uint32_t value);
extern void bb_rf_take_without_start(void);
extern void bthw_isr_do(void);
extern void bthw_thread_post(void);
extern void os_interrupt_enter(void);
extern void os_interrupt_leave(void);

uint8_t bb_rf_read_inside_upper[8]
    __attribute__((weak, section(".sbss"), aligned(4)));

/* This callback returns normally to the platform dispatcher. It must not use
 * interrupt("machine") or issue mret; the outer vector owns CPU context. */
static void BT_SECTION(".com_text.irq") baseband_platform_get_of_officer(void)
{
    os_interrupt_enter();
    BT_REG(0x3f8) &= ~UINT32_C(3);
    bthw_thread_post();
    os_interrupt_leave();
}

void baseband_platform_pass_by_hello(void)
{
    /* 0x3a4 is PLL1DIV in the public SDK register header. Preserve all four
     * volatile RMW operations, including the intermediate states. */
    BT_REG(0x3a4) &= UINT32_C(0xff000fff);
    BT_REG(0x3a4) |= UINT32_C(0x00eba000);
    BT_REG(0x3a4) &= UINT32_C(0xfffff7ff);
    BT_REG(0x3a4) &= ~UINT32_C(0x1c);
}

void BT_SECTION(".com_text.bthw") bthw_isr_do_plus(void)
{
    bthw_isr_do();
    BT_REG(0x3f8) |= UINT32_C(3);
}

void bthw_irq_init(void)
{
    bb_rf_read_inside_upper[0] = 0;
    bb_rf_read_inside_upper[1] = (uint8_t)bt_get_packet(255);
    baseband_platform_pass_by_hello();
    (void)register_isr(1, baseband_platform_get_of_officer);

    uint32_t vector_base = BT_REG(0x54c); /* PICADR */
    uint32_t slot = vector_base + UINT32_C(0x1e0);
    uint32_t delta = (uint32_t)(uintptr_t)bb_rf_take_without_start - slot;
    if (delta < UINT32_C(0x100000)) {
        /* The vendor accepts only nonnegative offsets below 1 MiB and does
         * not check alignment. Preserve this narrower-than-JAL range test. */
        uint32_t jal = ((delta << 20) & UINT32_C(0x7fe00000)) |
                       (delta & UINT32_C(0x000ff000)) |
                       ((delta << 9) & UINT32_C(0x00100000)) |
                       UINT32_C(0xef); /* jal ra, target */
        BT_REG(slot) = jal;
        BT_REG(0x548) |= UINT32_C(1) << 22; /* PICPR */
        BT_REG(0x544) |= UINT32_C(1) << 22; /* PICEN */
    }
}
