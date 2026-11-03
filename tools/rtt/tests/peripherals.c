/* Execute actual BSP callbacks against host MMIO and RT-Thread stubs. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "board.h"
volatile uint32_t test_spi[6], test_ports[4][9], test_timers[5][8];
volatile uint32_t test_wdt, test_clk, test_adc[16], test_func;
volatile uint32_t extra_regs[32];
static rt_tick_t tick;
static int complete_spi, last_cs, adc_result, adc_calls;
static int critical, in_irq;
static struct rt_spi_bus *registered_bus;
static int attach_error, configure_error, detached;
rt_size_t rt_strlen(const char *s) { return strlen(s); }
rt_device_t rt_device_find(const char *name) {
    return registered_bus && !strcmp(name, "spi1") ? &registered_bus->parent : NULL;
}
rt_err_t rt_spi_bus_attach_device_cspin(struct rt_spi_device *device,
    const char *name, const char *bus_name, rt_base_t pin, void *user) {
    (void)name; (void)bus_name; (void)user;
    device->bus = registered_bus;
    device->cs_pin = pin;
    if (!registered_bus->owner) registered_bus->owner = device;
    return attach_error;
}
rt_err_t rt_spi_configure(struct rt_spi_device *device, struct rt_spi_configuration *cfg) {
    device->config = *cfg;
    return configure_error;
}
rt_err_t rt_spi_bus_detach_device_cspin(struct rt_spi_device *device) {
    detached++;
    if (device->bus->owner == device) device->bus->owner = NULL;
    device->bus = NULL;
    return RT_EOK;
}
rt_thread_t rt_thread_self(void) { return (rt_thread_t)1; }
rt_uint8_t rt_interrupt_get_nest(void) { return in_irq; }
rt_uint16_t rt_critical_level(void) { return critical; }
rt_base_t rt_hw_interrupt_disable(void) { return 1; }
void rt_hw_interrupt_enable(rt_base_t level) { (void)level; }
uint32_t get_sysclk_nhz(void) { return 48000000; }
void hal_rcu_periph_clk_enable(uint32_t clock) { (void)clock; }
rt_tick_t rt_tick_get(void) {
    /* Each new byte starts at an even tick; model the W1C pending reset. */
    if (!(tick & 1)) SPI1CON &= ~BIT(16);
    if (complete_spi && (tick & 1)) { SPI1CON |= BIT(16); SPI1BUF = 0xa5; complete_spi--; }
    return tick++;
}
rt_tick_t rt_tick_from_millisecond(rt_int32_t ms) { return ms; }
void rt_pin_write(rt_base_t pin, rt_ssize_t value) { (void)pin; last_cs = value; }
void rt_set_errno(rt_err_t error) { (void)error; }
void hal_adc_enable(uint8_t enable) { (void)enable; }
void hal_adc_start(uint32_t channel) { (void)channel; adc_calls++; }
hal_error_t hal_adc_poll_for_conversion(uint32_t timeout) { (void)timeout; return adc_result; }
int rt_kprintf(const char *fmt, ...) { (void)fmt; return 0; }
void rt_interrupt_enter(void) {}
void rt_interrupt_leave(void) {}
void rt_assert_handler(const char *ex, const char *func, rt_size_t line) {
    fprintf(stderr, "%s:%lu: %s\n", func, (unsigned long)line, ex); abort();
}
rt_isr_handler_t rt_hw_interrupt_install(int vector, rt_isr_handler_t handler, void *p, const char *name) {
    (void)vector; (void)p; (void)name; return handler;
}
rt_err_t rt_device_hwtimer_register(rt_hwtimer_t *timer, const char *name, void *user) {
    (void)name; timer->parent.user_data = user; return RT_EOK;
}
rt_err_t rt_hw_watchdog_register(rt_watchdog_t *wdt, const char *name, rt_uint32_t flag, void *data) {
    (void)wdt; (void)name; (void)flag; (void)data; return RT_EOK;
}
#include "../../../rtt/bsp/libraries/hal_drivers/drv_spi.c"
#undef LOG_TAG
#include "../../../rtt/bsp/libraries/hal_drivers/drv_adc.c"
#undef LOG_TAG
#undef DBG_TAG
#include "../../../rtt/bsp/libraries/hal_drivers/drv_hwtimer.c"
#undef LOG_TAG
#undef DBG_TAG
#include "../../../rtt/bsp/libraries/hal_drivers/drv_wdt.c"

int main(void) {
    PICCON = 1;
    struct rt_spi_bus bus = {0};
    struct rt_spi_device dev = {.bus = &bus, .cs_pin = 0};
    struct rt_spi_configuration cfg = {.data_width = 8, .mode = RT_SPI_MSB, .max_hz = 5000000};
    uint8_t tx = 0x55, rx = 0;
    struct rt_spi_message msg = {.send_buf = &tx, .length = 1, .cs_take = 1, .cs_release = 1};
    assert(spi1_configure(&dev, &cfg) == RT_EOK);
    assert(SPI1BAUD == 9); /* 48MHz/10 <= requested 5MHz */
    dev.cs_pin = BSP_SPI1_MAPPING == 6 ? 16 : 19;
    assert(spi1_configure(&dev, &cfg) == -RT_EINVAL);
    dev.cs_pin = 0;
    if (BSP_SPI1_MAPPING == 6) {
        assert((GPIOE_BASE[3] & 0x1c) == 0x10); /* DI=PE4 only input */
        assert((GPIOE_BASE[5] & 0x1c) == 0x1c);
    }
    in_irq = 1;
    assert(spi1_configure(&dev, &cfg) == -RT_EBUSY);
    in_irq = 0; critical = 1;
    assert(spi1_xfer(&dev, &msg) == -RT_EBUSY);
    critical = 0;
    PICCON = 0;
    assert(spi1_xfer(&dev, &msg) == -RT_EBUSY);
    PICCON = 1;
    assert(!ab32_spi1_power_busy());
    spi1_bus.lock.owner = (rt_thread_t)1;
    assert(ab32_spi1_power_busy());
    spi1_bus.lock.owner = NULL;
    cfg.mode |= RT_SPI_CPHA;
    assert(spi1_configure(&dev, &cfg) == -RT_ENOSYS);
    cfg.mode = RT_SPI_MSB; cfg.max_hz = 0;
    assert(spi1_configure(&dev, &cfg) == -RT_EINVAL);
    cfg.max_hz = 5000000; dev.config = cfg;
    complete_spi = 1;
    assert(spi1_xfer(&dev, &msg) == 1 && last_cs == PIN_HIGH);
    msg.recv_buf = &rx;
    assert(spi1_xfer(&dev, &msg) == -RT_ENOSYS); /* no fake full-duplex */
    msg.send_buf = NULL; SPI1CON = 1; complete_spi = 1; tick = 0;
    assert(spi1_xfer(&dev, &msg) == 1 && rx == 0xa5);
    uint8_t pair[2] = {0};
    msg.recv_buf = pair; msg.length = 2;
    complete_spi = 1; SPI1CON = 1; tick = 0;
    assert(spi1_xfer(&dev, &msg) == -RT_ETIMEOUT);
    assert(pair[0] == 0xa5 && pair[1] == 0); /* partial frame must fail */
    msg.recv_buf = &rx; msg.length = 1;
    complete_spi = 0; SPI1CON = 1; tick = (rt_tick_t)-50;
    bus.owner = &dev;
    assert(spi1_xfer(&dev, &msg) == -RT_ETIMEOUT);
    assert(SPI1CON == 0 && last_cs == PIN_HIGH && bus.owner == NULL);

    /* Attach/configure uses RTT's deferred EBUSY convention. Registration
     * failures must clear the owner without unregistering another device. */
    struct rt_spi_device attached = {0};
    registered_bus = &bus; bus.parent.type = RT_Device_Class_SPIBUS;
    configure_error = -RT_EBUSY; bus.owner = &dev;
    assert(ab32_spi1_attach(&attached, "sensor", PIN_NONE, 1000000) == RT_EOK);
    assert(attached.bus == &bus && bus.owner == &dev && detached == 0);
    configure_error = -RT_EIO; bus.owner = NULL;
    assert(ab32_spi1_attach(&attached, "sensor", PIN_NONE, 1000000) == -RT_EIO);
    assert(!attached.bus && !bus.owner && detached == 1);
    attach_error = -RT_ENOMEM;
    assert(ab32_spi1_attach(&attached, "sensor", PIN_NONE, 1000000) == -RT_ENOMEM);
    assert(!attached.bus && !bus.owner && detached == 1);
    assert(ab32_spi1_attach(&attached, "sensor", 7, 1000000) == -RT_EINVAL);

    struct rt_adc_device adc = {0}; uint32_t value = 0x1234;
    adc.parent.user_data = (void *)test_adc;
    assert(ab32_get_adc_value(&adc, -1, &value) == -RT_EINVAL);
    assert(ab32_get_adc_value(&adc, 16, &value) == -RT_EINVAL && adc_calls == 0);
    adc_result = HAL_ETIMEOUT;
    assert(ab32_get_adc_value(&adc, 0, &value) == -RT_ETIMEOUT && value == 0x1234);
    adc_result = HAL_EOK; test_adc[3] = 777;
    assert(ab32_get_adc_value(&adc, 3, &value) == RT_EOK && value == 777);

    assert(ab32_hwtimer_init() == RT_EOK);
    rt_hwtimer_t *timer = &ab32_hwtimer_obj[TIM3_INDEX].time_device;
    timer->freq = 1000000;
    assert(timer_start(timer, 1000, HWTIMER_MODE_ONESHOT) == RT_EOK);
    assert(TIM3_BASE[TMRxPR] == 47999 && TIM3_BASE[TMRxCPND] == BIT(16));
    assert(timer_start(timer, 0xffffffffu, HWTIMER_MODE_PERIOD) == -RT_EINVAL);
    rt_uint32_t freq = 999999;
    assert(timer_ctrl(timer, HWTIMER_CTRL_FREQ_SET, &freq) == -RT_EINVAL);
    TIM2_BASE[TMRxCON] = BIT(0) | BIT(7);
    TIM4_BASE[TMRxCON] = BIT(0) | BIT(7) | BIT(16);
    TIM5_BASE[TMRxCON] = BIT(0) | BIT(7);
    timer2_4_5_isr(0, NULL);
    assert(ab32_hwtimer_obj[TIM2_INDEX].time_device.overflow == 0);
    assert(ab32_hwtimer_obj[TIM4_INDEX].time_device.overflow == 1);
    assert(ab32_hwtimer_obj[TIM5_INDEX].time_device.overflow == 0);
    assert(TIM4_BASE[TMRxCPND] == BIT(16));

    uint32_t seconds = 3;
    assert(wdt_control(NULL, RT_DEVICE_CTRL_WDT_SET_TIMEOUT, &seconds) == RT_EOK);
    assert(WDTCON == ((0xau << 24) | (5u << 20)));
    assert(wdt_control(NULL, RT_DEVICE_CTRL_WDT_GET_TIMEOUT, &seconds) == RT_EOK && seconds == 4);
    seconds = 0;
    assert(wdt_control(NULL, RT_DEVICE_CTRL_WDT_SET_TIMEOUT, &seconds) == -RT_EINVAL);
    assert(wdt_control(NULL, RT_DEVICE_CTRL_WDT_GET_TIMEOUT, NULL) == -RT_EINVAL);
    puts("PASS: SPI timeout/wrap/CS/unsupported mode, ADC errors, timer IRQ/overflow, WDT units");
    return 0;
}
