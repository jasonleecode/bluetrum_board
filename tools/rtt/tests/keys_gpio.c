/* Real GPIO/HAL and board-key state machines; deterministic MMIO/RTOS model. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "board.h"
volatile unsigned long gpio_ports[4][13], gpio_wkup[5], gpio_maps[3];
#define AB32_GPIO_TEST 1
#include "../../../rtt/bsp/libraries/hal_libraries/ab32vg1_hal/source/ab32vg1_hal_gpio.c"
#include "../../../rtt/bsp/libraries/hal_drivers/drv_gpio.c"
#include "../../../rtt/bsp/app/board_keys.c"

static unsigned locked, irq_depth, callbacks[27], wakes, created, enabled_irq, fail_create;
static unsigned key_lock_depth, callback_lock_depth, pin_modes;
static rt_tick_t ticks;
static struct rt_thread fake_thread;
rt_base_t rt_hw_interrupt_disable(void) { assert(!locked && !irq_depth); locked = 1; return 0x10003; }
void rt_hw_interrupt_enable(rt_base_t level) { assert(locked && level == 0x10003); locked = 0; }
void rt_interrupt_enter(void) { assert(!irq_depth); irq_depth = 1; }
void rt_interrupt_leave(void) { assert(irq_depth); irq_depth = 0; }
void rt_hw_irq_enable(int v) { assert(locked && v == IRQ_GPIO_IRQ); enabled_irq = 1; }
void rt_hw_irq_disable(int v) { assert(locked && v == IRQ_GPIO_IRQ); enabled_irq = 0; }
rt_isr_handler_t rt_hw_interrupt_install(int v, rt_isr_handler_t h, void *p, const char *n)
{ assert(locked && v == IRQ_GPIO_IRQ && h == ab32_pin_irq_handler && !p && !n); return NULL; }
void gpio_test_ack(rt_uint32_t bits)
{ assert(locked || irq_depth); assert(!(bits & ~(GPIO_MANAGED_WAKE << 16))); WKUPCPND = bits; WKUPEDG &= ~bits; }
void gpio_test_edges(rt_uint32_t bits)
{ assert(locked || irq_depth); assert(!(bits & ~0xffu)); WKUPEDG = (WKUPEDG & ~0xffu) | bits; }
rt_err_t rt_sem_init(rt_sem_t s, const char *n, rt_uint32_t v, rt_uint8_t f)
{ (void)n; (void)f; assert(s == &gpio_wake && !v); return 0; }
rt_err_t rt_sem_detach(rt_sem_t s) { (void)s; return 0; }
rt_err_t rt_sem_release(rt_sem_t s) { assert(irq_depth && s == &gpio_wake); wakes++; return 0; }
rt_err_t rt_sem_take(rt_sem_t s, rt_int32_t t) { (void)s; (void)t; return -RT_ETIMEOUT; }
rt_err_t rt_mutex_init(rt_mutex_t m, const char *n, rt_uint8_t f) { (void)m; (void)n; (void)f; return 0; }
rt_err_t rt_mutex_detach(rt_mutex_t m) { (void)m; return 0; }
rt_err_t rt_mutex_take(rt_mutex_t m, rt_int32_t t)
{ assert(!locked && !irq_depth && t == RT_WAITING_FOREVER); if (m == &keys_lock) key_lock_depth++; else { assert(m == &gpio_callback_lock); callback_lock_depth++; } return 0; }
rt_err_t rt_mutex_release(rt_mutex_t m)
{ if (m == &keys_lock) { assert(key_lock_depth); key_lock_depth--; } else { assert(callback_lock_depth); callback_lock_depth--; } return 0; }
rt_thread_t rt_thread_create(const char *n, void (*f)(void *), void *p, rt_uint32_t stack, rt_uint8_t prio, rt_uint32_t slice)
{ (void)prio; (void)slice; assert(!strcmp(n, "gpioirq") && f == ab32_pin_irq_thread && !p && stack == 1024); created++; return fail_create ? NULL : &fake_thread; }
rt_err_t rt_thread_startup(rt_thread_t t) { assert(t == &fake_thread); return 0; }
rt_err_t rt_thread_delete(rt_thread_t t) { (void)t; return 0; }
int rt_device_pin_register(const char *n, const struct rt_pin_ops *ops, void *p)
{ assert(!strcmp(n, "pin") && ops == &_ab32_pin_ops && !p); return 0; }
rt_err_t rt_device_register(rt_device_t d, const char *n, rt_uint16_t f)
{ assert(d == &keys_device && !strcmp(n, "keys0") && f == RT_DEVICE_FLAG_RDONLY); return 0; }
rt_size_t rt_strlen(const char *s) { return strlen(s); }
void *rt_memcpy(void *d, const void *s, rt_ubase_t n) { return memcpy(d, s, n); }
int rt_kprintf(const char *f, ...) { (void)f; assert(!irq_depth && !locked); return 0; }
rt_tick_t rt_tick_get(void) { return ticks; }
void rt_pin_mode(rt_base_t pin, rt_uint8_t mode) { pin_modes |= 1u << pin; ab32_pin_mode(NULL, pin, mode); }
rt_ssize_t rt_pin_read(rt_base_t pin) { return ab32_pin_read(NULL, pin); }
rt_err_t rt_thread_mdelay(rt_int32_t ms) { ticks += ms; board_keys_poll(); return 0; }
static void cb(void *p) { unsigned pin = (uintptr_t)p; assert(!irq_depth && !locked && callback_lock_depth); callbacks[pin]++; }
static void irq(unsigned pending) { WKUPEDG |= pending << 16; ab32_pin_irq_handler(IRQ_GPIO_IRQ, NULL); }
static void cb_requeue(void *p)
{
    cb(p);
    /* Pin 2 has already been dispatched; pin 22 remains pending. */
    GPIOA |= 4u; irq(BIT(7));
    GPIOA &= ~4u; irq(BIT(6));
}
static void cb_detach(void *p)
{
    cb(p);
    assert(ab32_pin_dettach_irq(NULL, (uintptr_t)p) == 0);
}
static void gpio_tests(void)
{
    assert(rt_hw_pin_init() == 0 && ab32_pin_irq_init() == 0 && !created);
    assert(ab32_pin_get("PF.1") == 22 && ab32_pin_get("PF.0") == 21 && ab32_pin_get("PA.2") == 2);
    assert(ab32_pin_get("PB.5") == -RT_EINVAL && ab32_pin_get("PG.0") == -RT_EINVAL);
    assert(ab32_pin_read(NULL, 27) == -RT_EINVAL);
    GPIOF_BASE[GPIOxFEN] = 0xff;
    ab32_pin_mode(NULL, 22, PIN_MODE_INPUT_PULLUP);
    assert(!(GPIOF_BASE[GPIOxFEN] & 2) && (GPIOF_BASE[GPIOxFEN] & 1));
    assert((GPIOF_BASE[GPIOxDIR] & 2) && (GPIOF_BASE[GPIOxDE] & 2) && (GPIOF_BASE[GPIOxPU] & 2));
    GPIOE = 0xa5;
    ab32_pin_write(NULL, 14, PIN_HIGH); assert(GPIOE_BASE[GPIOxSET] == 2 && GPIOE == 0xa5);
    ab32_pin_write(NULL, 14, PIN_LOW); assert(GPIOE_BASE[GPIOxCLR] == 2 && GPIOE == 0xa5);
    assert(ab32_pin_attach_irq(NULL, 27, PIN_IRQ_MODE_FALLING, cb, NULL) == -RT_EINVAL);
    assert(ab32_pin_attach_irq(NULL, 22, PIN_IRQ_MODE_HIGH_LEVEL, cb, NULL) == -RT_ENOSYS);
    assert(ab32_pin_attach_irq(NULL, 22, PIN_IRQ_MODE_FALLING, NULL, NULL) == -RT_EINVAL);
    GPIOA = GPIOB = GPIOE = GPIOF = 0xff;
    const unsigned pins[] = {22, 21, 2};
    for (unsigned i = 0; i < 3; i++)
        assert(ab32_pin_attach_irq(NULL, pins[i], PIN_IRQ_MODE_FALLING, cb, (void *)(uintptr_t)pins[i]) == 0);
    assert(!PORTINTEN && !WKUPCON); /* attach does not enable hardware */
    fail_create = 1;
    assert(ab32_pin_irq_enable(NULL, 22, 1) == -RT_ENOMEM && !gpio_enabled);
    fail_create = 0;
    for (unsigned i = 0; i < 3; i++) assert(ab32_pin_irq_enable(NULL, pins[i], 1) == 0);
    assert(created == 2 && enabled_irq && (WKUPCON & 0xff) == 0xc0);
    assert(ab32_pin_attach_irq(NULL, 22, PIN_IRQ_MODE_FALLING, cb, NULL) == -RT_EBUSY);
    GPIOF &= ~3u; GPIOA &= ~4u; irq(BIT(6));
    assert(!callbacks[22] && gpio_pending == (BIT(22) | BIT(21) | BIT(2)) && wakes == 1);
    gpio_dispatch(); assert(callbacks[22] == 1 && callbacks[21] == 1 && callbacks[2] == 1);
    assert(!(PORTINTEDG & (BIT(22) | BIT(21) | BIT(2)))); /* rearmed for release */
    GPIOF |= 3u; GPIOA |= 4u; irq(BIT(7)); gpio_dispatch();
    assert(callbacks[22] == 1 && callbacks[21] == 1); /* release isn't a falling callback */
    GPIOF &= ~2u; irq(BIT(6));
    assert(ab32_pin_dettach_irq(NULL, 22) == 0); /* queued old event must not reach a new handler */
    gpio_dispatch(); assert(callbacks[22] == 1);
    assert((PORTINTEN & BIT(21)) && !(PORTINTEN & BIT(22)));
    assert(ab32_pin_irq_enable(NULL, 21, 0) == 0 && (PORTINTEN & BIT(2)));
    assert(ab32_pin_dettach_irq(NULL, 21) == 0 && ab32_pin_dettach_irq(NULL, 2) == 0 && !enabled_irq);
    assert(ab32_pin_attach_irq(NULL, 21, PIN_IRQ_MODE_RISING_FALLING, cb, (void *)21) == 0);
    assert(ab32_pin_irq_enable(NULL, 21, 1) == 0);
    GPIOF &= ~1u; irq(BIT(6)); gpio_dispatch();
    GPIOF |= 1u; irq(BIT(7)); gpio_dispatch(); assert(callbacks[21] == 3);
    assert(ab32_pin_dettach_irq(NULL, 21) == 0);
    GPIOF |= 3u; GPIOA |= 4u;
    for (unsigned i = 0; i < 3; i++)
    {
        assert(ab32_pin_attach_irq(NULL, pins[i], PIN_IRQ_MODE_FALLING,
            pins[i] == 21 ? cb_requeue : cb, (void *)(uintptr_t)pins[i]) == 0);
        assert(ab32_pin_irq_enable(NULL, pins[i], 1) == 0);
    }
    unsigned prior_wakes = wakes, prior_key3 = callbacks[2];
    GPIOF &= ~3u; GPIOA &= ~4u; irq(BIT(6)); gpio_dispatch();
    assert(gpio_pending == BIT(2) && wakes == prior_wakes + 2);
    gpio_dispatch(); assert(!gpio_pending && callbacks[2] == prior_key3 + 2);
    for (unsigned i = 0; i < 3; i++) assert(ab32_pin_dettach_irq(NULL, pins[i]) == 0);
    /* Reattach after detaching a queued event, and detach recursively inside callback. */
    assert(ab32_pin_attach_irq(NULL, 21, PIN_IRQ_MODE_RISING, cb, (void *)21) == 0);
    assert(ab32_pin_irq_enable(NULL, 21, 1) == 0);
    GPIOF |= 1u; irq(BIT(7));
    assert(ab32_pin_dettach_irq(NULL, 21) == 0);
    assert(ab32_pin_attach_irq(NULL, 21, PIN_IRQ_MODE_FALLING, cb_detach, (void *)21) == 0);
    assert(ab32_pin_irq_enable(NULL, 21, 1) == 0);
    unsigned prior_key2 = callbacks[21];
    gpio_dispatch(); assert(callbacks[21] == prior_key2);
    GPIOF &= ~1u; irq(BIT(6)); gpio_dispatch();
    assert(callbacks[21] == prior_key2 + 1 && !gpio_enabled && !pin_irq_table[21].hdr);
    /* Dedicated PA7 path, correct W1C high bits, preserve unrelated RTC edge/enable. */
    WKUPCON = BIT(5) | BIT(16); WKUPEDG = BIT(5) | BIT(21);
    assert(ab32_pin_attach_irq(NULL, 7, PIN_IRQ_MODE_RISING, cb, (void *)7) == 0);
    assert(ab32_pin_irq_enable(NULL, 7, 1) == 0 && (WKUPCON & BIT(5)) && (WKUPEDG & BIT(5)));
    assert(!(PORTINTEN & BIT(7)) && !(WKUPEDG & 1));
    irq(1); assert(WKUPCPND == BIT(16) && (WKUPEDG & BIT(21))); gpio_dispatch(); assert(callbacks[7] == 1);
    assert(ab32_pin_dettach_irq(NULL, 7) == 0 && (WKUPCON & BIT(5)));
    puts("PASS: GPIO mux return, atomic SET/CLR, per-pin shared IRQs, both edges, detach/disable isolation, IRQ during dispatch, recursive detach, W1C/RTC preservation, lazy worker failure/retry");
}
static void sample(uint32_t t, unsigned pressed)
{
    ticks = t;
    GPIOF = (GPIOF & ~3ul) | ((pressed & 1) ? 0 : 2) | ((pressed & 2) ? 0 : 1);
    GPIOA = (GPIOA & ~4ul) | ((pressed & 4) ? 0 : 4);
    board_keys_poll();
}
static struct ab32_key_event expect(unsigned key, unsigned kind)
{
    uint8_t b[sizeof(struct ab32_key_event) + 1];
    struct ab32_key_event e;
    assert(keys_read(&keys_device, 0, b + 1, sizeof(e)) == sizeof(e));
    memcpy(&e, b + 1, sizeof(e)); assert(e.key == key && e.kind == kind);
    return e;
}
static void keys_tests(void)
{
    struct ab32_keys_status s;
    assert(board_keys_init() == 0 && pin_modes == (BIT(22) | BIT(21) | BIT(2)));
    sample(0, 0); sample(10, 1); sample(20, 0); sample(30, 1); sample(59, 1);
    assert(!status.queued); sample(60, 1); expect(1, AB32_KEY_DOWN);
    sample(859, 1); assert(!status.queued); sample(860, 1);
    assert(expect(1, AB32_KEY_LONG).held_ms == 800);
    sample(900, 1); assert(!status.queued);
    sample(910, 0); sample(920, 1); sample(930, 0); sample(960, 0);
    assert(expect(1, AB32_KEY_UP).held_ms == 900);
    sample(1000, 7); sample(1030, 7);
    for (unsigned i = 1; i <= 3; i++) expect(i, AB32_KEY_DOWN);
    sample(1100, 0); sample(1130, 0);
    for (unsigned i = 1; i <= 3; i++) expect(i, AB32_KEY_UP);
    for (unsigned i = 0; i < 4; i++) { sample(1200 + i * 100, 7); sample(1230 + i * 100, 7); sample(1240 + i * 100, 0); sample(1270 + i * 100, 0); }
    assert(status.queued == 8 && status.dropped == 16 && !status.pressed_mask);
    struct ab32_key_event batch[8]; assert(keys_read(&keys_device, 0, batch, sizeof(batch)) == sizeof(batch));
    assert(batch[7].kind == AB32_KEY_UP && batch[7].key == 3);
    sample(UINT32_MAX - 10, 2); sample(19, 2); expect(2, AB32_KEY_DOWN);
    sample(819, 2); assert(expect(2, AB32_KEY_LONG).held_ms == 800);
    sample(900, 0); sample(930, 0); expect(2, AB32_KEY_UP);
    /* Read failure breaks the debounce interval instead of inventing a key. */
    sample(1000, 4); rt_ssize_t bad[] = {1, 1, -RT_EIO}; keys_sample(bad, 1020);
    sample(1030, 4); sample(1059, 4); assert(!status.queued);
    sample(1060, 4); expect(3, AB32_KEY_DOWN);
    assert(status.read_errors == 1 && keys_control(NULL, AB32_KEYS_GET_STATUS, &s) == 0 && s.pressed_mask == 4);
    assert(keys_read(NULL, 0, &s, 1) == -RT_EINVAL);
    assert(keys_control(NULL, AB32_KEYS_GET_STATUS, NULL) == -RT_EINVAL);
    sample(1100, 0); sample(1130, 0); expect(3, AB32_KEY_UP);
    char *watch[] = {"keys", "watch", "1"}; ticks = UINT32_MAX - 200;
    assert(keys(3, watch) == 0 && ticks == 799);
    watch[2] = "31"; assert(keys(3, watch) == -RT_EINVAL);
    assert(!locked && !irq_depth && !key_lock_depth && !callback_lock_depth);
    puts("PASS: three keys, bounce on both edges, simultaneous presses, long once, held duration, overflow, unaligned reads, read failure, wrap and bounded watch");
}
int main(void) { gpio_tests(); keys_tests(); }
