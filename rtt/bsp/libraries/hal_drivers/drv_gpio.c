/*
 * Copyright (c) 2020-2021, Bluetrum Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2020-11-19     greedyhao         first version
 * 2025-05-15     kurisaW           support gpio interrupt
 */

#include "drv_gpio.h"
#include <interrupt.h>

#ifdef RT_USING_PIN

// #define DRV_DEBUG
#define LOG_TAG             "drv.gpio"
#include <drv_log.h>

struct port_info
{
    rt_uint8_t start_pin;
    rt_uint8_t delta_pin;
    rt_uint8_t total_pin;
};

/* It needs to be adjusted to the hardware. */
static const struct port_info port_table[] =
{
    {0, 8, 0},      /* PA0-PA7 */
    {0, 5, 8},      /* PB0-PB4 */
    {0, 8, 13},     /* PE0-PE7 */
    {0, 6, 21},     /* PF0-PF5 */
};

static const hal_sfr_t port_sfr[] =
{
    GPIOA_BASE,
    GPIOB_BASE,
    GPIOE_BASE,
    GPIOF_BASE,
};

#ifdef BSP_USING_PIN_IRQ
/* GPIO callbacks are per pin, never per shared wakeup circuit. */
static struct ab32_pin_irq pin_irq_table[27];
static struct rt_semaphore gpio_wake;
static struct rt_mutex gpio_callback_lock;
static rt_thread_t gpio_thread;
static rt_bool_t gpio_ready;
static rt_uint32_t gpio_enabled, gpio_pending, gpio_levels;
#define GPIO_MANAGED_WAKE 0xdfu /* circuits 0..4, 6, 7; preserve RTC/WKO 5 */
#define GPIO_RAM rt_section(".irq.gpio") __attribute__((noinline))
#ifdef AB32_GPIO_TEST
extern void gpio_test_ack(rt_uint32_t bits);
extern void gpio_test_edges(rt_uint32_t bits);
#define GPIO_ACK(bits) gpio_test_ack(bits)
#define GPIO_EDGES(bits) gpio_test_edges(bits)
#else
#define GPIO_ACK(bits) (WKUPCPND = (bits))
#define GPIO_EDGES(bits) (WKUPEDG = (bits))
#endif

#endif /* BSP_USING_PIN_IRQ */

static rt_uint8_t _pin_port(rt_uint32_t pin)
{
    rt_uint8_t port = 0;
    for (port = 0; port < 3; port++) {
        if (pin < (port_table[port].total_pin + port_table[port].delta_pin)) {
            break;
        }
    }
    return port;
}

#define PIN_NUM(port, no)       ((rt_uint8_t)(port_table[port].total_pin + no - port_table[port].start_pin))
#define PIN_PORT(pin)           _pin_port(pin)
#define PORT_SFR(port)          (port_sfr[(port)])

static rt_base_t ab32_pin_get(const char *name)
{
    rt_base_t pin = 0;
    int hw_port_num, hw_pin_num = 0;
    int i, name_len;

    if (name == RT_NULL)
        return -RT_EINVAL;
    name_len = rt_strlen(name);

    if ((name_len < 4) || (name_len >= 6))
    {
        return -RT_EINVAL;
    }
    if ((name[0] != 'P') || (name[2] != '.'))
    {
        return -RT_EINVAL;
    }

    if ((name[1] >= 'A') && (name[1] <= 'B'))
    {
        hw_port_num = (int)(name[1] - 'A');
    }
    else if ((name[1] >= 'E') && (name[1] <= 'F'))
    {
        hw_port_num = (int)(name[1] - 'A') - 2;  /* Without 'C' and 'D'. */
    }
    else
    {
        return -RT_EINVAL;
    }

    for (i = 3; i < name_len; i++)
    {
        if (name[i] < '0' || name[i] > '9')
            return -RT_EINVAL;
        hw_pin_num *= 10;
        hw_pin_num += name[i] - '0';
    }

    if (hw_pin_num >= port_table[hw_port_num].delta_pin)
        return -RT_EINVAL;
    pin = PIN_NUM(hw_port_num, hw_pin_num);
    LOG_D("name=%s", name);
    LOG_D("hw_port_num=%d hw_pin_num=%d pin=%d", hw_port_num, hw_pin_num, pin);

    return pin;
}

static void ab32_pin_write(rt_device_t dev, rt_base_t pin, rt_uint8_t value)
{
    if (pin < 0 || pin >= 27)
        return;
    rt_uint8_t port = PIN_PORT(pin);
    rt_uint8_t gpio_pin  = pin - port_table[port].total_pin;
    hal_gpio_write(PORT_SFR(port), gpio_pin, (rt_uint8_t)value);
}

static rt_ssize_t ab32_pin_read(rt_device_t dev, rt_base_t pin)
{
    if (pin < 0 || pin >= 27)
        return -RT_EINVAL;
    rt_uint8_t port = PIN_PORT(pin);
    rt_uint8_t gpio_pin  = pin - port_table[port].total_pin;
    return hal_gpio_read(PORT_SFR(port), gpio_pin);
}

static void ab32_pin_mode(rt_device_t dev, rt_base_t pin, rt_uint8_t mode)
{
    struct gpio_init gpio_init = {0};
    if (pin < 0 || pin >= 27)
        return;
    rt_uint8_t port = PIN_PORT(pin);

    gpio_init.pin = BIT(pin - port_table[port].total_pin);
    gpio_init.de  = GPIO_DIGITAL;
    gpio_init.af_con = GPIO_AFDIS;
    LOG_D("port=%d pin=%d", port, gpio_init.pin);

    switch (mode)
    {
    case PIN_MODE_INPUT:
        gpio_init.pull = GPIO_NOPULL;
        gpio_init.dir = GPIO_DIR_INPUT;
        break;
    case PIN_MODE_INPUT_PULLUP:
        gpio_init.pull = GPIO_PULLUP;
        gpio_init.dir = GPIO_DIR_INPUT;
        break;
    case PIN_MODE_INPUT_PULLDOWN:
        gpio_init.pull = GPIO_PULLDOWN;
        gpio_init.dir = GPIO_DIR_INPUT;
        break;
    case PIN_MODE_OUTPUT:
    case PIN_MODE_OUTPUT_OD:
    default:
        gpio_init.pull = GPIO_NOPULL;
        gpio_init.dir = GPIO_DIR_OUTPUT;
        break;
    }
    rt_base_t level = rt_hw_interrupt_disable();
    hal_gpio_init(PORT_SFR(port), &gpio_init);
    rt_hw_interrupt_enable(level);
}

#ifdef BSP_USING_PIN_IRQ
GPIO_RAM static rt_int32_t get_wakeup_circuit(rt_base_t pin)
{
    /* Special interrupt pins */
    if (pin == PIN_NUM(0, 7)) return WAKEUP_CRICUIT_0; // PA7
    if (pin == PIN_NUM(1, 1)) return WAKEUP_CRICUIT_1; // PB1
    if (pin == PIN_NUM(1, 2)) return WAKEUP_CRICUIT_2; // PB2
    if (pin == PIN_NUM(1, 3)) return WAKEUP_CRICUIT_3; // PB3
    if (pin == PIN_NUM(1, 4)) return WAKEUP_CRICUIT_4; // PB4
    /* WAKEUP_CRICUIT_5 is for RTC (WKO), assuming not a GPIO pin */
    /* Other pins use WAKEUP_CRICUIT_6 (falling) or WAKEUP_CRICUIT_7 (rising) */
    return -1; /* Shared PORT_INT circuit. */
}

GPIO_RAM static rt_uint32_t gpio_sample(void)
{
    return (GPIOA & 0xffu) | ((GPIOB & 0x1fu) << 8) |
           ((GPIOE & 0xffu) << 13) | ((GPIOF & 0x3fu) << 21);
}

/* Called with IRQs masked. Hardware provides one edge selector per pin;
 * generic pins are armed for the opposite level, so release is tracked even
 * for a falling-only subscriber. This is not a high-speed edge counter. */
GPIO_RAM static void gpio_configure_irqs(void)
{
    rt_uint32_t circuits = 0, generic = 0, edges = WKUPEDG & 0x20u;
    for (unsigned pin = 0; pin < 27; pin++)
    {
        if (!(gpio_enabled & BIT(pin))) continue;
        int circuit = get_wakeup_circuit(pin);
        if (circuit < 0) generic |= BIT(pin);
        else
        {
            circuits |= BIT(circuit);
            if (pin_irq_table[pin].mode == PIN_IRQ_MODE_FALLING ||
                (pin_irq_table[pin].mode == PIN_IRQ_MODE_RISING_FALLING && (gpio_levels & BIT(pin))))
                edges |= BIT(circuit);
        }
    }
    if (generic) { circuits |= BIT(6) | BIT(7); edges |= BIT(6); }
    PORTINTEN = (PORTINTEN & ~0x07ffffffu) | generic;
    PORTINTEDG = (PORTINTEDG & ~generic) | (gpio_levels & generic);
    GPIO_EDGES(edges); /* low edge bits only, never write pending bits */
    rt_uint32_t con = (WKUPCON & ~(GPIO_MANAGED_WAKE | BIT(16))) | circuits;
    if (con & 0xffu) con |= BIT(16);
    WKUPCON = con;
}

GPIO_RAM void ab32_pin_irq_handler(int vector, void *args)
{
    (void)vector; (void)args;
    rt_interrupt_enter();
    rt_uint32_t pending = (WKUPEDG >> 16) & GPIO_MANAGED_WAKE & WKUPCON;
    if (pending)
    {
        GPIO_ACK(pending << 16);
        rt_uint32_t levels = gpio_sample(), notify = 0;
        for (unsigned pin = 0; pin < 27; pin++)
        {
            rt_uint32_t bit = BIT(pin);
            if (!(gpio_enabled & bit)) continue;
            int circuit = get_wakeup_circuit(pin);
            if (circuit >= 0)
            {
                if (pending & BIT(circuit)) notify |= bit;
            }
            else if ((levels ^ gpio_levels) & bit)
            {
                rt_uint8_t mode = pin_irq_table[pin].mode;
                if (mode == PIN_IRQ_MODE_RISING_FALLING ||
                    ((levels & bit) ? mode == PIN_IRQ_MODE_RISING : mode == PIN_IRQ_MODE_FALLING))
                    notify |= bit;
            }
        }
        gpio_levels = levels;
        gpio_configure_irqs();
        if (notify)
        {
            gpio_pending |= notify;
            /* Always leave a wake token: a new event may target a pin the
             * worker already visited while other pending pins remain. */
            rt_sem_release(&gpio_wake);
        }
    }
    rt_interrupt_leave();
}

GPIO_RAM static rt_bool_t gpio_take_pin(unsigned pin)
{
    rt_base_t level = rt_hw_interrupt_disable();
    rt_bool_t take = !!(gpio_pending & BIT(pin));
    gpio_pending &= ~BIT(pin);
    rt_hw_interrupt_enable(level);
    return take;
}
static void gpio_dispatch(void)
{
    for (unsigned pin = 0; pin < 27; pin++)
    {
        /* Detach/disable waits for an in-flight callback. RTT mutex recursion
         * permits a callback to detach itself. No callback executes in ISR. */
        rt_mutex_take(&gpio_callback_lock, RT_WAITING_FOREVER);
        if (gpio_take_pin(pin) && (gpio_enabled & BIT(pin)) && pin_irq_table[pin].hdr)
            pin_irq_table[pin].hdr(pin_irq_table[pin].args);
        rt_mutex_release(&gpio_callback_lock);
    }
}
static void ab32_pin_irq_thread(void *parameter)
{
    (void)parameter;
    while (1)
        if (rt_sem_take(&gpio_wake, RT_WAITING_FOREVER) == RT_EOK) gpio_dispatch();
}

GPIO_RAM static void gpio_change_pin(rt_base_t pin, rt_bool_t enable)
{
    rt_base_t level = rt_hw_interrupt_disable();
    gpio_pending &= ~BIT(pin);
    if (enable)
    {
        if (!gpio_enabled)
        {
            GPIO_ACK(GPIO_MANAGED_WAKE << 16);
            rt_hw_interrupt_install(IRQ_GPIO_IRQ, ab32_pin_irq_handler, RT_NULL, RT_NULL);
        }
        gpio_levels = (gpio_levels & ~BIT(pin)) | (gpio_sample() & BIT(pin));
        gpio_enabled |= BIT(pin);
    }
    else gpio_enabled &= ~BIT(pin);
    gpio_configure_irqs();
    /* Correct API: vector enable is not restoration of global IRQ flags. */
    if (gpio_enabled) rt_hw_irq_enable(IRQ_GPIO_IRQ);
    else if (!(WKUPCON & BIT(5))) rt_hw_irq_disable(IRQ_GPIO_IRQ);
    rt_hw_interrupt_enable(level);
}
static rt_err_t ab32_pin_attach_irq(struct rt_device *device, rt_base_t pin,
                                    rt_uint8_t mode, void (*hdr)(void *), void *args)
{
    (void)device;
    if (pin < 0 || pin >= 27 || !hdr) return -RT_EINVAL;
    if (mode != PIN_IRQ_MODE_FALLING && mode != PIN_IRQ_MODE_RISING &&
        mode != PIN_IRQ_MODE_RISING_FALLING) return -RT_ENOSYS;
    if (!gpio_ready) return -RT_EIO;
    rt_mutex_take(&gpio_callback_lock, RT_WAITING_FOREVER);
    rt_err_t rc = RT_EOK;
    if (pin_irq_table[pin].hdr) rc = -RT_EBUSY;
    else
    {
        pin_irq_table[pin].hdr = hdr;
        pin_irq_table[pin].args = args;
        pin_irq_table[pin].mode = mode;
    }
    rt_mutex_release(&gpio_callback_lock);
    return rc;
}
static rt_err_t ab32_pin_dettach_irq(struct rt_device *device, rt_base_t pin)
{
    (void)device;
    if (pin < 0 || pin >= 27) return -RT_EINVAL;
    if (!gpio_ready) return -RT_EIO;
    rt_mutex_take(&gpio_callback_lock, RT_WAITING_FOREVER);
    gpio_change_pin(pin, RT_FALSE);
    pin_irq_table[pin].hdr = RT_NULL;
    pin_irq_table[pin].args = RT_NULL;
    rt_mutex_release(&gpio_callback_lock);
    return RT_EOK;
}
static rt_err_t ab32_pin_irq_enable(struct rt_device *device, rt_base_t pin, rt_uint8_t enabled)
{
    (void)device;
    if (pin < 0 || pin >= 27 || enabled > 1) return -RT_EINVAL;
    if (!gpio_ready) return -RT_EIO;
    rt_err_t rc = RT_EOK;
    rt_mutex_take(&gpio_callback_lock, RT_WAITING_FOREVER);
    if (!pin_irq_table[pin].hdr) { rc = -RT_EINVAL; goto out; }
    if (enabled && !gpio_thread)
    {
        gpio_thread = rt_thread_create("gpioirq", ab32_pin_irq_thread, RT_NULL, 1024, 15, 5);
        if (!gpio_thread) { rc = -RT_ENOMEM; goto out; }
        rc = rt_thread_startup(gpio_thread);
        if (rc) { rt_thread_delete(gpio_thread); gpio_thread = RT_NULL; goto out; }
    }
    gpio_change_pin(pin, enabled);
out:
    rt_mutex_release(&gpio_callback_lock);
    return rc;
}
static int ab32_pin_irq_init(void)
{
    rt_err_t rc = rt_sem_init(&gpio_wake, "gpioirq", 0, RT_IPC_FLAG_FIFO);
    if (rc) return rc;
    rc = rt_mutex_init(&gpio_callback_lock, "gpioirq", RT_IPC_FLAG_PRIO);
    if (rc) { rt_sem_detach(&gpio_wake); return rc; }
    gpio_ready = RT_TRUE;
    return RT_EOK;
}
INIT_PREV_EXPORT(ab32_pin_irq_init);
#endif /* BSP_USING_PIN_IRQ */

static const struct rt_pin_ops _ab32_pin_ops =
{
    .pin_mode = ab32_pin_mode,
    .pin_write = ab32_pin_write,
    .pin_read = ab32_pin_read,
    .pin_get = ab32_pin_get,
#ifdef BSP_USING_PIN_IRQ
    .pin_attach_irq = ab32_pin_attach_irq,
    .pin_detach_irq = ab32_pin_dettach_irq,
    .pin_irq_enable = ab32_pin_irq_enable,
#endif
};

int rt_hw_pin_init(void)
{
    return rt_device_pin_register("pin", &_ab32_pin_ops, RT_NULL);
}

#endif
