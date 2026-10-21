/* SPDX-License-Identifier: Apache-2.0
 * SPI1 register sequences recovered from driver_spi.o; see
 * docs/lib-analysis/SPI_I2C_RECOVERY.md. Polling uses hardware shifting.
 * Deliberately excludes unverified CPOL/CPHA and simultaneous TX/RX.
 */
#include "drv_common.h"
#include "board.h"
#include "drv_spi.h"

#ifdef BSP_USING_SPI1
#ifndef BSP_SPI1_MAPPING
#define BSP_SPI1_MAPPING 4
#endif
#ifndef BSP_SPI1_TIMEOUT_MS
#define BSP_SPI1_TIMEOUT_MS 100
#endif
#if BSP_SPI1_MAPPING < 1 || BSP_SPI1_MAPPING > 5
#error "SPI1 mapping must be 1..5"
#endif
#if BSP_SPI1_TIMEOUT_MS < 1 || BSP_SPI1_TIMEOUT_MS > 1000
#error "SPI1 timeout must be 1..1000 ms"
#endif

static struct rt_spi_bus spi1_bus;

static rt_bool_t spi1_cs_valid(rt_base_t pin)
{
    /* RT-Thread pin indices for the selected DI/CLK/DO triplet. */
    static const rt_uint8_t first[] = {2, 5, 8, 18, 21};
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

static void spi1_map(void)
{
    static const hal_sfr_t ports[] = {GPIOA_BASE, GPIOA_BASE, GPIOB_BASE, GPIOE_BASE, GPIOF_BASE};
    static const rt_uint8_t inputs[] = {2, 5, 0, 5, 0};
    hal_sfr_t port = ports[BSP_SPI1_MAPPING - 1];
    rt_uint32_t input = BIT(inputs[BSP_SPI1_MAPPING - 1]);
    rt_uint32_t outputs = (input << 1) | (input << 2);
    /* DIR, DE, FEN, PU offsets from recovered spi_io_mapping. */
    port[5] |= input | outputs;
    port[4] |= input | outputs;
    port[3] = (port[3] | input) & ~outputs;
    port[7] |= input;
    FUNCMCON1 |= 0xf000;
    FUNCMCON1 |= BSP_SPI1_MAPPING << 12;
}

static rt_err_t spi1_configure(struct rt_spi_device *device,
                              struct rt_spi_configuration *cfg)
{
    rt_uint32_t clock = get_sysclk_nhz();
    rt_uint32_t div;
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
    return done ? (rt_ssize_t)done : error;
}

static const struct rt_spi_ops spi1_ops = {spi1_configure, spi1_xfer};

rt_err_t ab32_spi1_attach(struct rt_spi_device *device, const char *name,
                          rt_base_t cs_pin, rt_uint32_t max_hz)
{
    struct rt_spi_configuration cfg = {0};
    rt_device_t found;
    rt_err_t ret;
    rt_uint32_t clock = get_sysclk_nhz();
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
    if (ret != RT_EOK) rt_spi_bus_detach_device_cspin(device);
    return ret;
}

static int rt_hw_spi1_init(void)
{
    return rt_spi_bus_register(&spi1_bus, "spi1", &spi1_ops);
}
INIT_BOARD_EXPORT(rt_hw_spi1_init);
#endif
