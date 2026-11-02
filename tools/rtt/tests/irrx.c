/* Real driver, modeled SFR write-one-clear/mapping commands; no IR hardware. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "board.h"
static volatile uint32_t ir_regs[7], gpio_e[8];
volatile uint32_t extra_regs[32];
#define IRRXCON ir_regs[0]
#define IRRXDAT ir_regs[1]
#define IRRXCPND ir_regs[2]
#define IRRXERR0 ir_regs[3]
#define IRRXERR1 ir_regs[4]
#define FUNCMCON2 ir_regs[5]
#define CLKGAT1 ir_regs[6]
#define GPIOEDIR gpio_e[0]
#define GPIOEDE gpio_e[1]
#define GPIOEPU gpio_e[2]
#define GPIOEFEN gpio_e[3]
#define GPIOEPD gpio_e[4]
#define GPIOEPU200K gpio_e[5]
#define GPIOEPU300 gpio_e[6]
#define GPIOEPD300 gpio_e[7]
#define AB32_IRRX_TEST 1
#include "../../../rtt/bsp/libraries/hal_drivers/drv_irrx.c"

static int locked, irq_depth, mutex_locked, irq_enabled, writes, installed, printed;
static uint32_t ticks, inject_raw;
static rt_isr_handler_t handler;
static uint32_t raw(uint16_t address, uint8_t cmd)
{ return address | (uint32_t)cmd << 16 | (uint32_t)(uint8_t)~cmd << 24; }
void irrx_test_write(volatile rt_uint32_t *reg, rt_uint32_t value)
{
    assert(locked || irq_depth);
    writes++;
    if (reg == &IRRXCPND)
    {
        assert(!(value & ~IR_PENDING));
        IRRXCPND = value;
        IRRXCON &= ~value;
        if (inject_raw) { IRRXDAT = inject_raw; IRRXCON |= IR_RX; inject_raw = 0; }
    }
    else if (reg == &FUNCMCON2)
    {
        /* zero means no effect, 15 clears; only the IR field may be written */
        assert(!(value & ~(15u << 20)));
        uint32_t field = value >> 20;
        if (field) FUNCMCON2 = (FUNCMCON2 & ~(15u << 20)) | (field == 15 ? 0 : value);
    }
    else
    {
        if (reg == &IRRXCON && value) assert(installed && irq_enabled);
        *reg = value;
    }
}
rt_base_t rt_hw_interrupt_disable(void) { assert(!locked && !irq_depth); locked = 1; return 123; }
void rt_hw_interrupt_enable(rt_base_t level) { assert(locked && level == 123); locked = 0; }
void rt_interrupt_enter(void) { assert(!irq_depth); irq_depth = 1; }
void rt_interrupt_leave(void) { assert(irq_depth == 1); irq_depth = 0; }
rt_tick_t rt_tick_get(void) { return ticks; }
void rt_hw_irq_disable(int v) { assert(locked && v == IRQ_IRRX_VECTOR); irq_enabled = 0; }
rt_isr_handler_t rt_hw_interrupt_install(int v, rt_isr_handler_t h, void *p, const char *n)
{ assert(locked && v == IRQ_IRRX_VECTOR && !p && !n); installed++; irq_enabled = 1; handler = h; return NULL; }
rt_err_t rt_mutex_init(rt_mutex_t m, const char *n, rt_uint8_t f)
{ assert(!locked && m == &ir_control_lock && !strcmp(n, "irrx") && f == RT_IPC_FLAG_PRIO); return 0; }
rt_err_t rt_mutex_take(rt_mutex_t m, rt_int32_t timeout)
{ assert(!locked && !irq_depth && !mutex_locked && m == &ir_control_lock && timeout == RT_WAITING_FOREVER); mutex_locked = 1; return 0; }
rt_err_t rt_mutex_release(rt_mutex_t m)
{ assert(!locked && mutex_locked && m == &ir_control_lock); mutex_locked = 0; return 0; }
rt_err_t rt_device_register(rt_device_t d, const char *n, rt_uint16_t f)
{ assert(d == &ir_device && !strcmp(n, "ir0") && f == RT_DEVICE_FLAG_RDONLY); return 0; }
void *rt_memcpy(void *d, const void *s, rt_ubase_t n)
{ assert(!locked && !irq_depth); return memcpy(d, s, n); }
int rt_kprintf(const char *f, ...) { (void)f; assert(!locked && !irq_depth); printed++; return 0; }
rt_err_t rt_thread_mdelay(rt_int32_t ms)
{ assert(!locked && !irq_depth && ms == 20); ticks += ms; return 0; }
static void receive(uint32_t data, uint32_t flags)
{
    IRRXDAT = data; IRRXCON |= flags;
    assert(handler);
    handler(IRQ_IRRX_VECTOR, NULL);
    assert(!irq_depth && !locked && !(IRRXCON & flags));
}
static void expect(uint8_t kind, uint32_t data)
{
    struct ab32_ir_event e;
    assert(irrx_read(&ir_device, 0, &e, sizeof(e)) == sizeof(e));
    assert(e.raw == data && e.kind == kind && e.tick == ticks);
    assert(!e.reserved[0] && !e.reserved[1] && !e.reserved[2]);
}
int main(void)
{
    struct ab32_ir_status snapshot;
    struct ab32_ir_event e;
    uint32_t old_pins[8], old_mapping = (2u << 20) | (3u << 4) | (1u << 16);
    for (unsigned i = 0; i < 8; i++) gpio_e[i] = old_pins[i] = 0x81 | ((i & 1) ? IR_PIN : 0);
    FUNCMCON2 = old_mapping; CLKGAT1 = BIT(8);
    assert(ab32_irrx_init() == 0 && !writes && !installed); /* boot does not touch IR */
    assert(irrx_read(&ir_device, 0, &e, sizeof(e)) == 0);
    assert(irrx_control(&ir_device, AB32_IR_GET_STATUS, NULL) == -RT_EINVAL);
    assert(irrx_control(&ir_device, 123, NULL) == -RT_ENOSYS);
    assert(irrx_control(&ir_device, AB32_IR_STOP, NULL) == 0 && !writes);
    IRRXCON = IR_PENDING; /* stale boot flags */
    assert(irrx_control(&ir_device, AB32_IR_START, NULL) == 0 && state.running);
    assert(IRRXCON == 3 && CLKGAT1 == (BIT(8) | IR_GATE));
    assert((FUNCMCON2 >> 20) == 7 && (FUNCMCON2 & 0xfffff) == (old_mapping & 0xfffff));
    assert(IRRXERR0 == ((1000u << 16) | 1000u));
    assert(IRRXERR1 == ((1718u << 20) | (250u << 10) | 125u));
    for (unsigned i = 0; i < 8; i++) assert(gpio_e[i] == (0x81 | (i < 3 ? IR_PIN : 0)));
    int before = writes;
    assert(irrx_control(&ir_device, AB32_IR_START, NULL) == 0 && writes == before);
    receive(0, 0); assert(writes == before); /* unrelated shared-vector call */
    ticks = 7;
    uint32_t a = raw(0xff00, 0x12), b = raw(0x1234, 0xe7);
    receive(a, IR_RX); expect(AB32_IR_DOWN, a);
    receive(a, IR_RX); expect(AB32_IR_REPEAT, a);
    receive(b, IR_RX); expect(AB32_IR_UP, a); expect(AB32_IR_DOWN, b);
    receive(0, IR_RELEASE); expect(AB32_IR_UP, b);
    receive(0, IR_RELEASE); assert(!state.queued);
    receive(raw(0xff00, 0), IR_RX | IR_RELEASE);
    expect(AB32_IR_DOWN, raw(0xff00, 0)); expect(AB32_IR_UP, raw(0xff00, 0));
    assert(!state.pressed); /* valid command zero, accumulated flags */
    receive(a, IR_RX); expect(AB32_IR_DOWN, a);
    receive(a ^ BIT(24), IR_RX); expect(AB32_IR_UP, a);
    assert(state.invalid == 1 && !state.pressed);
    receive(0, IR_RX); assert(state.invalid == 2 && !state.queued);
    /* The raw register changes during W1C: the first frame must be snapshotted. */
    inject_raw = b; IRRXDAT = a; IRRXCON |= IR_RX; handler(0, NULL);
    expect(AB32_IR_DOWN, a);
    assert(IRRXCON & IR_RX); handler(0, NULL);
    expect(AB32_IR_UP, a); expect(AB32_IR_DOWN, b);
    ticks = UINT32_MAX;
    receive(0, IR_RELEASE); expect(AB32_IR_UP, b);
    ticks = 0;
    receive(a, IR_RX); expect(AB32_IR_DOWN, a);
    for (unsigned i = 0; i < AB32_IR_QUEUE_SIZE + 3; i++) receive(a, IR_RX);
    receive(0, IR_RELEASE);
    assert(state.queued == 8 && state.dropped == 4 && !state.pressed);
    for (unsigned i = 0; i < 7; i++) expect(AB32_IR_REPEAT, a);
    expect(AB32_IR_UP, a); assert(!state.queued);
    receive(a, IR_RX);
    uint8_t unaligned[sizeof(e) + 1];
    assert(irrx_read(&ir_device, 0, unaligned + 1, sizeof(e)) == sizeof(e));
    memcpy(&e, unaligned + 1, sizeof(e)); assert(e.kind == AB32_IR_DOWN);
    assert(irrx_read(&ir_device, 0, &e, 1) == -RT_EINVAL);
    assert(irrx_read(&ir_device, 1, &e, sizeof(e)) == -RT_EINVAL);
    assert(irrx_read(&ir_device, 0, NULL, sizeof(e)) == -RT_EINVAL);
    assert(irrx_control(&ir_device, AB32_IR_STOP, NULL) == 0);
    expect(AB32_IR_UP, a);
    assert(!state.running && !irq_enabled && IRRXCON == 0 && CLKGAT1 == BIT(8));
    assert(FUNCMCON2 == old_mapping);
    for (unsigned i = 0; i < 8; i++) assert(gpio_e[i] == old_pins[i]);
    receive(a, IR_RX); assert(!state.queued); /* late IRQ must not publish */
    CLKGAT1 |= IR_GATE;
    assert(irrx_control(&ir_device, AB32_IR_START, NULL) == 0);
    assert(!state.frames && !state.dropped && !state.invalid);
    char *watch[] = {"irrx", "watch", "1"};
    ticks = UINT32_MAX - 200;
    assert(irrx(3, watch) == 0 && ticks == 799); /* bounded watch across tick wrap */
    watch[2] = "31"; assert(irrx(3, watch) == -RT_EINVAL);
    watch[2] = "abc"; assert(irrx(3, watch) == -RT_EINVAL);
    assert(irrx_control(&ir_device, AB32_IR_GET_STATUS, &snapshot) == 0 && snapshot.running);
    assert(irrx_control(&ir_device, AB32_IR_STOP, NULL) == 0 && (CLKGAT1 & IR_GATE));
    watch[2] = "1"; assert(irrx(3, watch) == -RT_EIO);
    puts("PASS: IR boot/start/stop, gate/pin/mapping preservation, NEC/extended commands, repeats, release, invalid frames, simultaneous flags, W1C snapshot, queue overflow, unaligned I/O, tick wrap and bounded watch");
}
