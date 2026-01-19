#include <rtthread.h>
#include <board.h>

// port_intsrc= {PG[4:0], PF[5:0], PE[7:0], PB[4:0], PA[7:0]}
// 这里的 PF0 就是 BIT(21)

#define IRQ_PORT_VECTOR          18   //PORT_INI的中断号为18

#if 0   // PF0
RT_SECTION(".com_text.str0")
const char strisr0[] = ">>[0x%X]_[0x%X]\n";
RT_SECTION(".com_text.str1")
const char strisr1[] = "portisr->";

RT_SECTION(".irq.port")
void port_isr(int vector, void *param)
{
    rt_interrupt_enter();
    rt_kprintf(strisr0,WKUPEDG,WKUPCPND);
    if (WKUPEDG & (BIT(6) << 16)) {
        WKUPCPND = (BIT(6) << 16);  //CLEAR PENDING
        rt_kprintf(strisr1);
    }
    rt_interrupt_leave();
}

void port_isr_test(void)     //sys_set_tmr_enable(1, 1); 前调用 测试OK
{
    GPIOFDE |= BIT(0);  GPIOFDIR |= BIT(0); GPIOFFEN &= ~BIT(0);
    GPIOFPU |= BIT(0);
    rt_hw_interrupt_install(IRQ_PORT_VECTOR, port_isr, RT_NULL, "port_isr");
    PORTINTEN |= BIT(21);
    PORTINTEDG |= BIT(21);  //falling edge;
    WKUPEDG |= BIT(6);     //falling edge
    WKUPCON = BIT(6) | BIT(16);  //falling edge wake iput //wakeup int en

    rt_kprintf("PORTINTEN = 0x%X, PORTINTEDG = 0x%X  WKUPEDG = 0x%X, WKUPCON = 0x%X\n", PORTINTEN, PORTINTEDG, WKUPEDG, WKUPCON);
    WDT_DIS();
    while(1) {
//       rt_kprintf("WKUPEDG = 0x%X\n", WKUPEDG);
//       rt_kprintf("GPIOF = 0x%X\n", GPIOF);
//       delay_ms(500);
    }
}

#else

RT_SECTION(".com_text.str0")
const char strisr0[] = ">>[0x%X]_[0x%X]\n";
RT_SECTION(".com_text.str1")
const char strisr1[] = "portisr->%d\n";

RT_SECTION(".irq.port")
void port_isr(int vector, void *param)
{
    rt_interrupt_enter();
    rt_kprintf(strisr0,WKUPEDG,WKUPCPND);
    if (WKUPEDG & (BIT(1) << 16)) {
        WKUPCPND = BIT(1) << 16;
        rt_kprintf(strisr1, 1);
    }
    if (WKUPEDG & (BIT(2) << 16)) {
        WKUPCPND = BIT(2) << 16;
        rt_kprintf(strisr1, 2);
    }
    rt_interrupt_leave();
}

int port_isr_test(void)
{
    rt_hw_interrupt_install(IRQ_PORT_VECTOR, port_isr, RT_NULL, "port_isr");
    GPIOBFEN &= ~(BIT(1) | BIT(2));
    GPIOBDE  |= (BIT(1) | BIT(2));
    GPIOBDIR |= (BIT(1) | BIT(2));
    GPIOBPU |= (BIT(1) | BIT(2));

    WKUPEDG = BIT(1) | BIT(2);
    WKUPCON = BIT(1) | BIT(2) | BIT(16);
    return 0;
}

#endif
