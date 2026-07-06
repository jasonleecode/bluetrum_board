#include <stdint.h>

#define DRV_VENDOR_MAX_IRQ 32u
#define DRV_VENDOR_SYSCLK_HZ 48000000u
#define DRV_VENDOR_DELAY_CYCLES_PER_US 12u

typedef void (*isr_t)(void);

static isr_t irq_table[DRV_VENDOR_MAX_IRQ];
static volatile uint32_t tick_count;
static volatile uint8_t bt_addr[6] = {0x41, 0x42, 0x00, 0x00, 0x00, 0x01};
static uint8_t ep2_isoc_storage[192] __attribute__((aligned(4)));

void *ep2_isoc = ep2_isoc_storage;

static void busy_delay(uint32_t loops)
{
    while (loops--) {
        __asm__ volatile ("nop");
    }
}

void *memcpy(void *dest, const void *src, unsigned long n)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;

    while (n--) {
        *d++ = *s++;
    }

    return dest;
}

void *memset(void *s, int c, unsigned long n)
{
    uint8_t *p = (uint8_t *)s;

    while (n--) {
        *p++ = (uint8_t)c;
    }

    return s;
}

int memcmp(const void *s1, const void *s2, unsigned long n)
{
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;

    while (n--) {
        if (*p1 != *p2) {
            return (int)*p1 - (int)*p2;
        }
        p1++;
        p2++;
    }

    return 0;
}

void os_interrupt_enter(void)
{
}

void os_interrupt_leave(void)
{
}

uint32_t os_get_interrupt_nest(void)
{
    return 0;
}

void *rt_thread_self(void)
{
    return 0;
}

isr_t register_isr(int vector, isr_t isr)
{
    if (vector < 0 || (uint32_t)vector >= DRV_VENDOR_MAX_IRQ) {
        return 0;
    }

    isr_t old = irq_table[vector];
    irq_table[vector] = isr;
    return old;
}

void interrupt_handler_c(void)
{
}

uint32_t hal_get_ticks(void)
{
    return tick_count;
}

uint32_t get_sysclk_nhz(void)
{
    return DRV_VENDOR_SYSCLK_HZ;
}

void hal_udelay(uint32_t nus)
{
    busy_delay(nus * DRV_VENDOR_DELAY_CYCLES_PER_US);
}

void hal_mdelay(uint32_t nms)
{
    while (nms--) {
        hal_udelay(1000);
        tick_count++;
    }
}

void hal_printf(const char *fmt, ...)
{
    (void)fmt;
}

void my_printf(const char *format, ...)
{
    (void)format;
}

void my_print_r(const void *buf, uint16_t cnt)
{
    (void)buf;
    (void)cnt;
}

void os_spiflash_lock(void)
{
}

void os_spiflash_unlock(void)
{
}

void os_cache_lock(void)
{
}

void os_cache_unlock(void)
{
}

void os_mq_ude_ctl_flow_post(void)
{
}

void os_mq_ude_ep0_setup_post(void)
{
}

void os_mq_ude_reset_post(void)
{
}

void sdadc_analog_aux_start(uint32_t channel, uint32_t gain)
{
    (void)channel;
    (void)gain;
}

void sdadc_analog_aux_exit(uint32_t channel)
{
    (void)channel;
}

void bt_get_local_bd_addr(uint8_t *addr)
{
    if (addr) {
        for (uint32_t i = 0; i < sizeof(bt_addr); i++) {
            addr[i] = bt_addr[i];
        }
    }
}

void hci_host_recv_packet(uint8_t *buf, int len)
{
    (void)buf;
    (void)len;
}

void nanos_event_set_trigger(void)
{
}

void bthw_thread_post(void)
{
}

void bthw_soft_kick(void)
{
}
