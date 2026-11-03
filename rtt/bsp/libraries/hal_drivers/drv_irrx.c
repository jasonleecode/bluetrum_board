/*
 * Copyright (c) 2020-2021, Bluetrum Development Team
 * SPDX-License-Identifier: Apache-2.0
 * Native NEC receiver on PE6 / IR_G7. See docs/rtt/IR.md for evidence and limits.
 * No worker, allocation, logging or application callback in the IRQ path.
 */
#include <board.h>
#include <rthw.h>
#include <interrupt.h>
#include "drv_irrx.h"
#include <finsh.h>
#include <stdlib.h>
#include <string.h>

#ifdef BSP_USING_IRRX
#if defined(BSP_USING_TIM3) || defined(BSP_USING_T3_PWM)
#error "IRRX and Timer3/PWM share IRQ 6; select only one owner"
#endif
#if defined(BSP_USING_SPI1) && BSP_SPI1_MAPPING == 4
#error "IRRX PE6 conflicts with SPI1 G4 clock"
#endif
#ifdef BSP_USING_IRRX_SW
#error "The old unvalidated software IR decoder is no longer supported"
#endif

#define IR_RX BIT(16)
#define IR_RELEASE BIT(17)
#define IR_PENDING (IR_RX | IR_RELEASE)
#define IR_PIN BIT(6)
#define IR_GATE BIT(3) /* CLKGAT1: RCU_IRRX(29) - RCU_FMAMFDT(26) */
#define IR_RAM rt_section(".irq.irrx") __attribute__((noinline))
#ifdef AB32_IRRX_TEST
extern void irrx_test_write(volatile rt_uint32_t *reg, rt_uint32_t value);
#define IR_WRITE(reg, value) irrx_test_write(&(reg), (value))
#else
#define IR_WRITE(reg, value) ((reg) = (value))
#endif

static struct rt_device ir_device;
static struct rt_mutex ir_control_lock;
static struct ab32_ir_event events[AB32_IR_QUEUE_SIZE];
static struct ab32_ir_status state;
static rt_uint8_t head, tail, saved_pins, saved_mapping, saved_gate;
static rt_tick_t last_frame;
static rt_bool_t release_pending;
#define IR_RELEASE_TICKS ((RT_TICK_PER_SECOND * 400u + 999u) / 1000u)
/* These addresses are read with interrupts masked: keep the table in SRAM. */
rt_section(".irq.irrx.pins")
static volatile rt_uint32_t *const irrx_pin_regs[] = {
    &GPIOEDIR, &GPIOEDE, &GPIOEPU, &GPIOEFEN,
    &GPIOEPD, &GPIOEPU200K, &GPIOEPU300, &GPIOEPD300
};

/* Called in IRQ context or with global interrupts masked. Preserve the newest
 * event (especially UP) on overflow, and expose loss explicitly in status. */
IR_RAM static void irrx_push(rt_uint8_t kind)
{
    struct ab32_ir_event *e;
    if (state.queued == AB32_IR_QUEUE_SIZE)
    {
        tail = (tail + 1) % AB32_IR_QUEUE_SIZE;
        state.dropped++;
    }
    else state.queued++;
    e = &events[head];
    e->raw = state.raw;
    e->tick = (rt_uint32_t)rt_tick_get();
    e->kind = kind;
    e->reserved[0] = e->reserved[1] = e->reserved[2] = 0;
    head = (head + 1) % AB32_IR_QUEUE_SIZE;
}
IR_RAM static void irrx_release(void)
{
    release_pending = RT_FALSE;
    if (state.pressed)
    {
        state.pressed = 0;
        irrx_push(AB32_IR_UP);
    }
}
/* Main calls every 10 ms. Hardware's ~110 ms timeout is shorter than some
 * NEC remotes' full-frame repeats (measured 157 ms, first gap ~314 ms).
 * Require both hardware release and 400 ms without a complete valid frame.
 * Short NEC repeats still defer hardware release; don't time them out solely
 * because they don't produce full-frame interrupts. */
IR_RAM void ab32_irrx_poll(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    if (state.running && state.pressed && release_pending &&
        (rt_tick_t)(rt_tick_get() - last_frame) >= IR_RELEASE_TICKS)
        irrx_release();
    rt_hw_interrupt_enable(level);
}
IR_RAM static void irrx_isr(int vector, void *param)
{
    rt_uint32_t pending, raw = 0;
    (void)vector; (void)param;
    rt_interrupt_enter();
    pending = IRRXCON & IR_PENDING;
    if (pending & IR_RX) raw = IRRXDAT;
    /* Snapshot data before clearing W1C. Do not RMW status/command registers. */
    if (pending) IR_WRITE(IRRXCPND, pending);
    if (state.running)
    {
        if (pending & IR_RX)
        {
            if ((((raw >> 16) ^ (raw >> 24)) & 0xffu) != 0xffu)
            {
                state.invalid++;
                irrx_release();
            }
            else
            {
                rt_uint8_t kind = AB32_IR_DOWN;
                state.frames++;
                if (state.pressed && state.raw == raw) kind = AB32_IR_REPEAT;
                else irrx_release();
                state.raw = raw;
                state.pressed = 1;
                last_frame = rt_tick_get();
                release_pending = RT_FALSE;
                irrx_push(kind);
            }
        }
        /* Delay the application UP; complete repeats can bridge this pending
         * release. Polling also resolves simultaneous RX/release flags. */
        if (pending & IR_RELEASE)
        {
            state.releases++;
            release_pending = state.pressed;
        }
    }
    rt_interrupt_leave();
}

/* Entire critical section executes in SRAM, including pin/clock RMW and the
 * ISR installation helper. The gate is restored; the shared GPIO clock stays on. */
IR_RAM static void irrx_switch(rt_bool_t on)
{
    rt_base_t level = rt_hw_interrupt_disable();
    if (!!on == !!state.running) goto out;
    rt_hw_irq_disable(IRQ_IRRX_VECTOR);
    if (on)
    {
        saved_gate = !!(CLKGAT1 & IR_GATE);
        CLKGAT0 |= BIT(17);
        CLKGAT1 |= IR_GATE;
        IR_WRITE(IRRXCON, 0);
        IR_WRITE(IRRXCPND, IR_PENDING);
        saved_pins = 0;
        for (unsigned i = 0; i < sizeof(irrx_pin_regs) / sizeof(irrx_pin_regs[0]); ++i)
        {
            if (*irrx_pin_regs[i] & IR_PIN) saved_pins |= BIT(i);
            if (i < 3) *irrx_pin_regs[i] |= IR_PIN; /* input, digital, pull-up */
            else *irrx_pin_regs[i] &= ~IR_PIN;
        }
        saved_mapping = (FUNCMCON2 >> 20) & 0xf;
        IR_WRITE(FUNCMCON2, 0xfu << 20);
        IR_WRITE(FUNCMCON2, 7u << 20); /* DataSheet: PE6 = IR_G7 */
        /* Vendor hardware profile: 1 MHz clock, 32 bits, ~110 ms release.
         * DAT/RPT tolerance 1001 us, ONE 251 us, ZERO 126 us. */
        IR_WRITE(IRRXERR0, (1000u << 16) | 1000u);
        IR_WRITE(IRRXERR1, (1718u << 20) | (250u << 10) | 125u);
        state = (struct ab32_ir_status){0};
        head = tail = 0;
        release_pending = RT_FALSE;
        state.running = 1;
        rt_hw_interrupt_install(IRQ_IRRX_VECTOR, irrx_isr, RT_NULL, RT_NULL);
        IR_WRITE(IRRXCON, BIT(1) | BIT(0));
    }
    else
    {
        IR_WRITE(IRRXCON, 0);
        IR_WRITE(IRRXCPND, IR_PENDING);
        irrx_release();
        state.running = 0;
        IR_WRITE(FUNCMCON2, 0xfu << 20);
        if (saved_mapping && saved_mapping != 0xf)
            IR_WRITE(FUNCMCON2, (rt_uint32_t)saved_mapping << 20);
        for (unsigned i = 0; i < sizeof(irrx_pin_regs) / sizeof(irrx_pin_regs[0]); ++i)
        {
            if (saved_pins & BIT(i)) *irrx_pin_regs[i] |= IR_PIN;
            else *irrx_pin_regs[i] &= ~IR_PIN;
        }
        if (!saved_gate) CLKGAT1 &= ~IR_GATE;
    }
out:
    rt_hw_interrupt_enable(level);
}
IR_RAM static rt_bool_t irrx_pop(struct ab32_ir_event *e)
{
    rt_base_t level = rt_hw_interrupt_disable();
    rt_bool_t found = state.queued != 0;
    if (found)
    {
        *e = events[tail];
        tail = (tail + 1) % AB32_IR_QUEUE_SIZE;
        state.queued--;
    }
    rt_hw_interrupt_enable(level);
    return found;
}
IR_RAM static void irrx_snapshot(struct ab32_ir_status *s)
{
    rt_base_t level = rt_hw_interrupt_disable();
    *s = state;
    rt_hw_interrupt_enable(level);
}
static rt_ssize_t irrx_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    struct ab32_ir_event e;
    rt_size_t n = 0;
    (void)dev;
    if (pos || (!buffer && size) || size % sizeof(e)) return -RT_EINVAL;
    if (size > sizeof(events)) size = sizeof(events);
    while (n < size && irrx_pop(&e))
    {
        rt_memcpy((rt_uint8_t *)buffer + n, &e, sizeof(e));
        n += sizeof(e);
    }
    return n;
}
static rt_err_t irrx_control(rt_device_t dev, int cmd, void *arg)
{
    (void)dev;
    if (cmd == AB32_IR_GET_STATUS)
    {
        struct ab32_ir_status s;
        if (!arg) return -RT_EINVAL;
        irrx_snapshot(&s);
        rt_memcpy(arg, &s, sizeof(s));
        return RT_EOK;
    }
    if (cmd != AB32_IR_START && cmd != AB32_IR_STOP) return -RT_ENOSYS;
    rt_mutex_take(&ir_control_lock, RT_WAITING_FOREVER);
    irrx_switch(cmd == AB32_IR_START);
    rt_mutex_release(&ir_control_lock);
    return RT_EOK;
}
static const struct rt_device_ops irrx_ops = {
    .read = irrx_read, .control = irrx_control
};
/* Global init symbol also identifies this feature to the linker RAM budget. */
int ab32_irrx_init(void)
{
    rt_err_t rc = rt_mutex_init(&ir_control_lock, "irrx", RT_IPC_FLAG_PRIO);
    if (rc) return rc;
    ir_device.type = RT_Device_Class_Miscellaneous;
    ir_device.ops = &irrx_ops;
    return rt_device_register(&ir_device, "ir0", RT_DEVICE_FLAG_RDONLY);
}
INIT_DEVICE_EXPORT(ab32_irrx_init);

static void irrx_print_event(const struct ab32_ir_event *e)
{
    unsigned addr = e->raw & 0xffffu;
    int standard = ((addr ^ (addr >> 8)) & 0xffu) == 0xffu;
    rt_kprintf("IR %s %s addr=%04x cmd=%02x raw=%08x tick=%u\n",
        e->kind == AB32_IR_UP ? "UP" : e->kind == AB32_IR_REPEAT ? "REPEAT" : "DOWN",
        standard ? "NEC" : "NEC-ext", standard ? addr & 0xffu : addr,
        (unsigned)((e->raw >> 16) & 0xff), (unsigned)e->raw, (unsigned)e->tick);
}
static int irrx(int argc, char **argv)
{
    struct ab32_ir_status s;
    if (argc == 2 && (!strcmp(argv[1], "start") || !strcmp(argv[1], "stop")))
        return irrx_control(&ir_device, !strcmp(argv[1], "start") ? AB32_IR_START : AB32_IR_STOP, RT_NULL);
    if (argc == 2 && !strcmp(argv[1], "status"))
    {
        irrx_snapshot(&s);
        rt_kprintf("ir0: running=%u pressed=%u raw=%08x queued=%u dropped=%u frames=%u invalid=%u releases=%u\n",
            (unsigned)s.running, (unsigned)s.pressed, (unsigned)s.raw, (unsigned)s.queued,
            (unsigned)s.dropped, (unsigned)s.frames, (unsigned)s.invalid, (unsigned)s.releases);
        return RT_EOK;
    }
    if ((argc == 2 && !strcmp(argv[1], "read")) ||
        ((argc == 2 || argc == 3) && !strcmp(argv[1], "watch")))
    {
        struct ab32_ir_event e;
        unsigned long seconds = 10;
        rt_tick_t begin;
        rt_bool_t watch = !strcmp(argv[1], "watch");
        if (argc == 3)
        {
            char *end;
            seconds = strtoul(argv[2], &end, 10);
            if (!*argv[2] || *end || seconds < 1 || seconds > 30) return -RT_EINVAL;
        }
        irrx_snapshot(&s);
        if (watch && !s.running) return -RT_EIO;
        begin = rt_tick_get();
        do
        {
            /* Bound each batch even if a noisy input keeps producing data. */
            for (unsigned i = 0; i < AB32_IR_QUEUE_SIZE && irrx_pop(&e); ++i)
                irrx_print_event(&e);
            if (!watch) break;
            rt_thread_mdelay(20);
        } while ((rt_tick_t)(rt_tick_get() - begin) < seconds * RT_TICK_PER_SECOND);
        return RT_EOK;
    }
    rt_kprintf("irrx start|stop|status|read|watch [1..30 seconds]\n");
    return -RT_EINVAL;
}
MSH_CMD_EXPORT(irrx, NEC infrared receiver on PE6);
#endif
