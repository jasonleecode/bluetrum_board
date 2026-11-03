/* SPDX-License-Identifier: Apache-2.0
 * SPI1 register sequences recovered from driver_spi.o; see
 * docs/lib-analysis/SPI_I2C_RECOVERY.md. Polling uses hardware shifting.
 * Deliberately excludes unverified CPOL/CPHA and simultaneous TX/RX.
 */
#include "drv_common.h"
#include "board.h"
#include "drv_spi.h"
#include <stdlib.h>

#ifdef BSP_USING_SPI1
#ifndef BSP_SPI1_MAPPING
#define BSP_SPI1_MAPPING 6
#endif
#ifndef BSP_SPI1_TIMEOUT_MS
#define BSP_SPI1_TIMEOUT_MS 100
#endif
#if BSP_SPI1_MAPPING < 1 || BSP_SPI1_MAPPING > 6
#error "SPI1 mapping must be 1..6"
#endif

#if defined(BSP_USING_UART0) && BSP_SPI1_MAPPING == 2
#error "SPI1 G2 conflicts with UART0/programming PA7"
#endif
#if defined(BSP_USING_UART1) && BSP_SPI1_MAPPING == 1
#error "SPI1 G1 conflicts with UART1 PA3/PA4"
#endif
#if defined(BSP_USING_UART2) && BSP_SPI1_MAPPING == 3
#error "SPI1 G3 conflicts with UART2 PB1/PB2"
#endif
#if defined(BSP_USING_I2S) && (!defined(BSP_I2S_MAPPING) || BSP_I2S_MAPPING == 1) && BSP_SPI1_MAPPING == 6
#error "SPI1 G6 conflicts with I2S PE2/PE3/PE4"
#endif
#if defined(BSP_USING_I2C1) && BSP_SPI1_MAPPING == 6 && \
    ((BSP_I2C1_SCL_PIN >= 15 && BSP_I2C1_SCL_PIN <= 17) || \
     (BSP_I2C1_SDA_PIN >= 15 && BSP_I2C1_SDA_PIN <= 17))
#error "SPI1 G6 and I2C1 cannot own the same pins"
#endif

#define SPI_RAM rt_section(".irq.spi") __attribute__((noinline))
#if BSP_SPI1_TIMEOUT_MS < 1 || BSP_SPI1_TIMEOUT_MS > 1000
#error "SPI1 timeout must be 1..1000 ms"
#endif

static struct rt_spi_bus spi1_bus;

/* Called by PM with scheduling/interrupts locked; never touch Flash here. */
SPI_RAM rt_bool_t ab32_spi1_power_busy(void)
{ return spi1_bus.lock.owner != RT_NULL; }

static rt_bool_t spi1_thread_context(void)
{
    return rt_thread_self() && !rt_interrupt_get_nest() &&
        !rt_critical_level() && (PICCON & 1u);
}

static rt_bool_t spi1_cs_valid(rt_base_t pin)
{
    /* RT-Thread pin indices for the selected DI/CLK/DO triplet. */
    static const rt_uint8_t first[] = {2, 5, 8, 18, 21, 15};
#ifdef BSP_USING_UART0
    if (pin == 6 || pin == 7) return RT_FALSE; /* programming header */
#endif
#ifdef BSP_USING_UART1
    if (pin == 3 || pin == 4) return RT_FALSE;
#endif
#ifdef BSP_USING_UART2
    if (pin == 9 || pin == 10) return RT_FALSE;
#endif
#ifdef BSP_USING_KEYS
    if (pin == 2 || pin == 21 || pin == 22) return RT_FALSE;
#endif
#ifdef BSP_USING_IRRX
    if (pin == 19) return RT_FALSE;
#endif
#ifdef BSP_USING_TFCARD
    if (pin >= 8 && pin <= 10) return RT_FALSE;
#endif
#ifdef BSP_USING_USB_CDC
    if (pin == 11 || pin == 12) return RT_FALSE;
#endif
#ifdef BSP_USING_LED_BREATH
    if (pin == 14) return RT_FALSE;
#endif
#ifdef BSP_USING_I2C1
    if (pin == BSP_I2C1_SCL_PIN || pin == BSP_I2C1_SDA_PIN) return RT_FALSE;
#endif
#ifdef BSP_USING_I2S
#if !defined(BSP_I2S_MAPPING) || BSP_I2S_MAPPING == 1
    if (pin >= 13 && pin <= 17) return RT_FALSE; /* PE0..4 */
#else
    if (pin >= 0 && pin <= 4) return RT_FALSE; /* PA0..4 */
#endif
#endif
    return pin >= 0 && pin <= 26 &&
        (pin < first[BSP_SPI1_MAPPING - 1] || pin > first[BSP_SPI1_MAPPING - 1] + 2);
}

SPI_RAM static void spi1_map(void)
{
    static const hal_sfr_t ports[] rt_section(".com_rodata.spi") =
        {GPIOA_BASE, GPIOA_BASE, GPIOB_BASE, GPIOE_BASE, GPIOF_BASE, GPIOE_BASE};
    static const rt_uint8_t inputs[] rt_section(".com_rodata.spi") = {2, 5, 0, 5, 0, 4};
    hal_sfr_t port = ports[BSP_SPI1_MAPPING - 1];
    rt_uint32_t input = BIT(inputs[BSP_SPI1_MAPPING - 1]);
    /* Datasheet G6 reverses the order: DI=PE4, CLK=PE3, DO=PE2. */
    rt_uint32_t outputs = BSP_SPI1_MAPPING == 6 ? BIT(3) | BIT(2) : (input << 1) | (input << 2);
    rt_base_t level = rt_hw_interrupt_disable();
    /* DIR, DE, FEN, PU offsets from recovered spi_io_mapping. */
    port[5] |= input | outputs;
    port[4] |= input | outputs;
    port[3] = (port[3] | input) & ~outputs;
    port[7] |= input;
    port[8] &= ~input;
    FUNCMCON1 |= 0xf000;
    FUNCMCON1 |= BSP_SPI1_MAPPING << 12;
    rt_hw_interrupt_enable(level);
}

static rt_err_t spi1_configure(struct rt_spi_device *device,
                              struct rt_spi_configuration *cfg)
{
    rt_uint32_t clock = get_sysclk_nhz();
    rt_uint32_t div;
    if (!spi1_thread_context()) return -RT_EBUSY;
    if (!cfg || !cfg->max_hz || !clock || cfg->data_width != 8)
        return -RT_EINVAL;
    if ((cfg->mode & ~(RT_SPI_CS_HIGH | RT_SPI_NO_CS)) != RT_SPI_MSB)
        return -RT_ENOSYS;
    /* Ceiling division: never exceed the peripheral's max_hz. */
    div = clock / cfg->max_hz + (clock % cfg->max_hz != 0);
    /* Conservative divider range pending full SPI1 register documentation. */
    if (!div || div > 256)
        return -RT_EINVAL;
    if (!(cfg->mode & RT_SPI_NO_CS) &&
        !spi1_cs_valid(device->cs_pin))
        return -RT_EINVAL;
    hal_rcu_periph_clk_enable(RCU_SPI1);
    SPI1CON = 0;
    SPI1BAUD = div - 1;
    spi1_map();
    SPI1CPND = BIT(16);
    SPI1CON = BIT(0);
    return RT_EOK;
}

static void spi1_cs(struct rt_spi_device *device, rt_bool_t active)
{
    if (!(device->config.mode & RT_SPI_NO_CS) && device->cs_pin != PIN_NONE)
        rt_pin_write(device->cs_pin,
                     active == !!(device->config.mode & RT_SPI_CS_HIGH) ? PIN_HIGH : PIN_LOW);
}

static rt_ssize_t spi1_xfer(struct rt_spi_device *device, struct rt_spi_message *msg)
{
    const rt_uint8_t *tx = msg->send_buf;
    rt_uint8_t *rx = msg->recv_buf;
    rt_size_t done = 0;
    rt_tick_t timeout = rt_tick_from_millisecond(BSP_SPI1_TIMEOUT_MS);
    rt_err_t error = -RT_EINVAL;

    if (!spi1_thread_context()) return -RT_EBUSY;

    if ((tx && rx) || (msg->length && !tx && !rx))
    {
        error = tx && rx ? -RT_ENOSYS : -RT_EINVAL;
        goto fail;
    }
    if (!(SPI1CON & BIT(0)))
    {
        error = -RT_EIO;
        goto fail;
    }
    if (msg->cs_take)
        spi1_cs(device, RT_TRUE);
    for (; done < msg->length; ++done)
    {
        rt_tick_t start = rt_tick_get();
        SPI1CPND = BIT(16);
        if (rx)
            SPI1CON |= BIT(4);
        else
            SPI1CON &= ~BIT(4);
        SPI1BUF = tx ? tx[done] : 0xff;
        while (!(SPI1CON & BIT(16)))
        {
            if ((rt_tick_t)(rt_tick_get() - start) >= timeout)
            {
                SPI1CON = 0;
                SPI1CPND = BIT(16);
                device->bus->owner = RT_NULL; /* next transfer reconfigures */
                error = -RT_ETIMEOUT;
                goto fail;
            }
        }
        if (rx)
            rx[done] = SPI1BUF;
    }
    SPI1CPND = BIT(16);
    if (msg->cs_release)
        spi1_cs(device, RT_FALSE);
    return done;
fail:
    /* Also releases CS held by an earlier message in this transaction. */
    spi1_cs(device, RT_FALSE);
    rt_set_errno(error);
    /* RTT chained-message helpers only check negative results. Returning a
     * short positive count here would let them report a failed frame as OK. */
    return error;
}

static const struct rt_spi_ops spi1_ops = {spi1_configure, spi1_xfer};

rt_err_t ab32_spi1_attach(struct rt_spi_device *device, const char *name,
                          rt_base_t cs_pin, rt_uint32_t max_hz)
{
    struct rt_spi_configuration cfg = {0};
    rt_device_t found;
    rt_err_t ret;
    rt_uint32_t clock = get_sysclk_nhz();
    if (!spi1_thread_context()) return -RT_EBUSY;
    if (!device || !name || !*name || rt_strlen(name) >= RT_NAME_MAX ||
        !max_hz || !clock || (cs_pin != PIN_NONE && !spi1_cs_valid(cs_pin)))
        return -RT_EINVAL;
    if (clock / max_hz + (clock % max_hz != 0) > 256)
        return -RT_EINVAL;
    if (rt_device_find(name)) return -RT_EBUSY;
    found = rt_device_find("spi1");
    if (!found || found->type != RT_Device_Class_SPIBUS) return -RT_ENOSYS;
    cfg.data_width = 8;
    cfg.mode = RT_SPI_MSB | (cs_pin == PIN_NONE ? RT_SPI_NO_CS : 0);
    cfg.max_hz = max_hz;
    /* Preload inactive CS before the framework switches the pin to output. */
    if (cs_pin != PIN_NONE) rt_pin_write(cs_pin, PIN_HIGH);
    ret = rt_spi_bus_attach_device_cspin(device, name, "spi1", cs_pin, RT_NULL);
    if (ret != RT_EOK)
    {
        /* Core may have assigned owner even though registration failed. */
        if (device->bus && device->bus->owner == device) device->bus->owner = RT_NULL;
        device->bus = RT_NULL;
        return ret;
    }
    ret = rt_spi_configure(device, &cfg);
    /* RTT returns EBUSY when another device owns the cached configuration;
     * it will apply this validated configuration on the next transfer. */
    if (ret == -RT_EBUSY) ret = RT_EOK;
    if (ret != RT_EOK) rt_spi_bus_detach_device_cspin(device);
    return ret;
}

static int rt_hw_spi1_init(void)
{
    return rt_spi_bus_register(&spi1_bus, "spi1", &spi1_ops);
}
/* Registration is thread-only so the SPI framework may reside in Flash. */
INIT_DEVICE_EXPORT(rt_hw_spi1_init);

#if defined(RT_USING_FINSH) && BSP_SPI1_MAPPING == 6
/* Empty-header diagnostic, not a substitute for a slave/logic-analyzer test.
 * Preserve other PE pins: the red LED and IR receiver share this GPIO bank. */
SPI_RAM static void spi1_test_pull(rt_bool_t high)
{
    rt_base_t level = rt_hw_interrupt_disable();
    GPIOE_BASE[7] = (GPIOE_BASE[7] & ~BIT(4)) | (high ? BIT(4) : 0);
    GPIOE_BASE[8] = (GPIOE_BASE[8] & ~BIT(4)) | (high ? 0 : BIT(4));
    rt_hw_interrupt_enable(level);
}

SPI_RAM static void spi1_test_restore(const rt_uint32_t *saved, rt_uint32_t mapping)
{
    rt_base_t level = rt_hw_interrupt_disable();
    SPI1CON = 0;
    SPI1CPND = BIT(16);
    for (unsigned i = 3; i <= 8; i++)
        GPIOE_BASE[i] = (GPIOE_BASE[i] & ~0x1cu) | (saved[i - 3] & 0x1cu);
    FUNCMCON1 |= 0xf000;
    if (mapping && mapping != 15) FUNCMCON1 |= mapping << 12;
    rt_hw_interrupt_enable(level);
}

static int spi1check(int argc, char **argv)
{
    struct rt_spi_device device = {0};
    rt_uint8_t tx[16], rx[16];
    rt_uint32_t saved[6], mapping, hz = 1000000;
    rt_err_t rc;
    char *end;
    if (argc > 2) return -RT_EINVAL;
    if (argc == 2)
    {
        hz = strtoul(argv[1], &end, 10);
        if (!*argv[1] || *end || !hz) return -RT_EINVAL;
    }
    if (!spi1_thread_context()) return -RT_EBUSY;
    if (!rt_device_find("spi1")) return -RT_ENOSYS;
    rc = rt_mutex_take(&spi1_bus.lock, 0);
    if (rc) return rc;
    /* Do not disrupt an application which already attached a peripheral. */
    if (spi1_bus.owner) { rc = -RT_EBUSY; goto unlock; }
    mapping = (FUNCMCON1 >> 12) & 15;
    for (unsigned i = 3; i <= 8; i++) saved[i - 3] = GPIOE_BASE[i];
    rc = ab32_spi1_attach(&device, "spitest", PIN_NONE, hz);
    if (rc) goto restore;
    rt_uint32_t clock = get_sysclk_nhz();
    rt_uint32_t divider = clock / hz + (clock % hz != 0);
    rt_kprintf("SPI1 G6 DI=PE4 CLK=PE3 DO=PE2, reset timing, requested=%u nominal=%u Hz\n",
               (unsigned)hz, (unsigned)(clock / divider));
    /* BAUD reads zero on V2 hardware; don't infer wire speed from readback. */
    rt_kprintf("divider written=%u BAUD readback=%08x\n",
               (unsigned)(divider - 1), (unsigned)SPI1BAUD);
    for (unsigned i = 0; i < sizeof(tx); i++) tx[i] = (i & 1) ? 0x55 : 0xaa;
    rt_ssize_t n = rt_spi_send(&device, tx, sizeof(tx));
    rt_kprintf("TX completed=%d/%u\n", (int)n, (unsigned)sizeof(tx));
    if (n != sizeof(tx)) { rc = n < 0 ? n : -RT_EIO; goto detach; }
    for (unsigned high = 0; high < 2; high++)
    {
        spi1_test_pull(high);
        rt_thread_mdelay(2);
        n = rt_spi_recv(&device, rx, sizeof(rx));
        rt_kprintf("RX pull-%s completed=%d data=", high ? "up" : "down", (int)n);
        if (n != sizeof(rx)) rc = n < 0 ? n : -RT_EIO;
        for (unsigned i = 0; n > 0 && i < (unsigned)n; i++)
        {
            rt_kprintf("%02x", rx[i]);
            if (rx[i] != (high ? 0xff : 0)) rc = -RT_EIO;
        }
        rt_kprintf("\n");
        if (rc) break;
    }
    if (!rc)
    {
        rt_tick_t start = rt_tick_get();
        unsigned count;
        for (count = 0; count < 4096; count += sizeof(tx))
        {
            n = rt_spi_send(&device, tx, sizeof(tx));
            if (n != sizeof(tx)) { rc = n < 0 ? n : -RT_EIO; break; }
        }
        rt_kprintf("TX benchmark bytes=%u elapsed_ticks=%u tick_hz=%u\n",
                   count, (unsigned)(rt_tick_get() - start), RT_TICK_PER_SECOND);
    }
detach:
    rt_spi_bus_detach_device_cspin(&device);
restore:
    spi1_test_restore(saved, mapping);
    rt_kprintf("SPI1 controller self-test: %s (%d); external timing/slave NOT verified\n",
               rc ? "FAIL" : "PASS", rc);
unlock:
    rt_mutex_release(&spi1_bus.lock);
    return rc;
}
MSH_CMD_EXPORT(spi1check, Empty PE2/PE3/PE4 header only: SPI controller test [Hz]);
#endif
#endif
