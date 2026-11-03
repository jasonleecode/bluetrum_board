/* SPDX-License-Identifier: Apache-2.0
 * Recovered AUDEC MP3 hardware back end; see docs/rtt/MP3.md for provenance.
 * No vendor codec object is linked. SRAM work area must be reserved at link time.
 */
#include "board.h"
#include "drv_audec.h"
#include <stdint.h>
#include <string.h>

#ifdef BSP_USING_AUDEC
#ifndef RT_USING_DEVICE_OPS
#error "AUDEC requires RT_USING_DEVICE_OPS"
#endif

#define AUDEC_CON    0x180u
#define AUDEC_KICK   0x184u
#define AUDEC_STATUS 0x188u
#define AUDEC_OUTPUT 0x190u
#define AUDEC_GATE   (1u << RCU_AUDEC)
#define WORK_GATE    (1u << RCU_RAM3)
#define AUDEC_IRQ    (1u << IRQ_AUDEC_VECTOR)
#define CLOCK_MASK   (15u << 13)
#define GUARD       0x5aa55aa5u
#define AUDEC_RAM rt_section(".irq.audec") __attribute__((noinline))

#ifdef AB32_AUDEC_TEST
rt_uint32_t audec_read(rt_uint32_t address);
void audec_write(rt_uint32_t address, rt_uint32_t value);
#else
static inline rt_uint32_t audec_read(rt_uint32_t address)
{ return *(volatile rt_uint32_t *)(uintptr_t)address; }
static inline void audec_write(rt_uint32_t address, rt_uint32_t value)
{ *(volatile rt_uint32_t *)(uintptr_t)address = value; }
#endif

static rt_uint8_t work[AB32_AUDEC_WORK_SIZE]
    __attribute__((section(".audec.work"), aligned(4), used));
static struct {
    rt_uint32_t before, samples[AB32_AUDEC_SAMPLES], after;
} dma __attribute__((aligned(16)));
static struct rt_device device;
static struct rt_mutex lock;
static rt_thread_t owner;
static struct ab32_audec_status stats;
static rt_bool_t ready, first;
static rt_uint32_t saved_gate, saved_clock;

rt_bool_t ab32_audec_power_busy(void)
{ return owner != RT_NULL || lock.owner != RT_NULL; }

/* Update ONLY owned fields: audio/FM also modify CLKCON2 and CLKGAT0. */
AUDEC_RAM static void configure_clock(rt_bool_t enable)
{
    rt_base_t level = rt_hw_interrupt_disable();
    if (enable)
    {
        CLKGAT0 |= AUDEC_GATE | WORK_GATE;
        CLKCON2 = (CLKCON2 & ~CLOCK_MASK) | (3u << 13); /* <=30 MHz at 120 MHz */
    }
    else
    {
        CLKGAT0 = (CLKGAT0 & ~(AUDEC_GATE | WORK_GATE)) | saved_gate;
        CLKCON2 = (CLKCON2 & ~CLOCK_MASK) | saved_clock;
    }
    rt_hw_interrupt_enable(level);
}

AUDEC_RAM static void stop_engine(void)
{
    audec_write(AUDEC_CON, 1u << 27);
    audec_write(AUDEC_KICK, 0);
    audec_write(AUDEC_STATUS, 0x003f3f00);
    audec_write(AUDEC_OUTPUT, 0);
    rt_base_t level = rt_hw_interrupt_disable();
    CLKGAT0 &= ~AUDEC_GATE;
    rt_hw_interrupt_enable(level);
}

static void reset_stream(void)
{
    configure_clock(RT_TRUE);
    audec_write(AUDEC_CON, 1u << 27);
    audec_write(AUDEC_KICK, 0);
    audec_write(AUDEC_STATUS, 0x003f3f00);
    audec_write(AUDEC_OUTPUT, 0);
    memset(work, 0, sizeof(work));
    first = RT_TRUE;
    stats.faulted = 0;
    stats.input_channels = 0;
    stats.last_error = RT_EOK;
}

static rt_bool_t valid_buffer(const void *buffer, rt_size_t bytes)
{
    uintptr_t begin = (uintptr_t)buffer, end = begin + bytes;
    uintptr_t dma_begin = (uintptr_t)&dma, dma_end = dma_begin + sizeof(dma);
    if (!begin || (begin & 3u) || end < begin) return RT_FALSE;
    if (begin < AB32_AUDEC_WORK_BASE + AB32_AUDEC_WORK_SIZE &&
        end > AB32_AUDEC_WORK_BASE) return RT_FALSE;
    return end <= dma_begin || begin >= dma_end;
}

static rt_err_t decode(const struct ab32_audec_granule *g)
{
    if (stats.faulted) return -RT_EIO;
    if (!g || g->block_type > 3 || g->block_type_right > 3 ||
        g->mode > AB32_AUDEC_MS_STEREO || g->timeout_ms > 1000 ||
        g->pcm_bytes < AB32_AUDEC_PCM_BYTES ||
        !valid_buffer(g->spectrum, AB32_AUDEC_SAMPLES * 4u) ||
        !valid_buffer(g->bands, AB32_AUDEC_BANDS * 4u) ||
        !valid_buffer(g->pcm, AB32_AUDEC_PCM_BYTES)) return -RT_EINVAL;
    rt_uint8_t channels = g->mode == AB32_AUDEC_MONO ? 1 : 2;
    if (channels == 1)
    {
        if (g->spectrum_right || g->bands_right || g->block_type_right) return -RT_EINVAL;
    }
    else if (!valid_buffer(g->spectrum_right, AB32_AUDEC_SAMPLES * 4u) ||
             !valid_buffer(g->bands_right, AB32_AUDEC_BANDS * 4u)) return -RT_EINVAL;
    /* Channel-count changes describe a new stream; require a history reset. */
    if (stats.input_channels && stats.input_channels != channels) return -RT_EINVAL;
    /* The timeout requires a live RTT tick, not just global IRQ enable. */
    if (!(PICEN & (1u << IRQ_TMR0_VECTOR)) || (TMR0CON & 0x81u) != 0x81u || !TMR0PR)
        return -RT_EBUSY;
    memcpy(work + 0x1000, g->spectrum, AB32_AUDEC_SAMPLES * 4u);
    memcpy(work + 0x2b00, g->bands, AB32_AUDEC_BANDS * 4u);
    if (channels == 2)
    {
        memcpy(work + 0x1d80, g->spectrum_right, AB32_AUDEC_SAMPLES * 4u);
        memcpy(work + 0x2b9c, g->bands_right, AB32_AUDEC_BANDS * 4u);
    }
    dma.before = dma.after = GUARD;
    memset(dma.samples, 0, sizeof(dma.samples));
    audec_write(AUDEC_STATUS, 0x003f0000);
    audec_write(AUDEC_OUTPUT, (rt_uint32_t)(uintptr_t)dma.samples);
    /* Recovered III_decode mapping: mode[9:8], extension[11:10],
     * L block[13:12], R block[16:15]. Keep MPEG-1/44.1k and RAM PCM fixed.
     * No raw register bits, downmix or unverified intensity-stereo mode. */
    rt_uint32_t format = g->mode == AB32_AUDEC_MONO ? 0x315u :
                         g->mode == AB32_AUDEC_MS_STEREO ? 0x915u : 0x15u;
    rt_uint32_t control = (1u << 26) | format | ((rt_uint32_t)g->block_type << 12) |
                         ((rt_uint32_t)g->block_type_right << 15);
    if (first) control |= 1u << 19;
    audec_write(AUDEC_CON, control);
    /* CRWN traps on FENCE. Buffers/MMIO are uncached; order compiler accesses. */
    __asm__ volatile ("" ::: "memory");
    rt_tick_t start = rt_tick_get();
    rt_tick_t timeout = rt_tick_from_millisecond(g->timeout_ms ? g->timeout_ms : 200);
    audec_write(AUDEC_KICK, 0x00a0003f);
    rt_uint32_t status;
    while (!((status = audec_read(AUDEC_STATUS)) & 0x20u) &&
           (rt_tick_t)(rt_tick_get() - start) < timeout)
        rt_thread_mdelay(1);
    __asm__ volatile ("" ::: "memory");
    stats.last_ticks = rt_tick_get() - start;
    stats.last_status = status;
    stats.last_control = audec_read(AUDEC_CON);
    rt_err_t rc = RT_EOK;
    if (!(status & 0x20u)) { ++stats.timeouts; rc = -RT_ETIMEOUT; }
    else if (dma.before != GUARD || dma.after != GUARD) rc = -RT_EIO;
    if (rc)
    {
        stop_engine(); /* private DMA buffer stays allocated even after a failure */
        stats.faulted = 1;
        ++stats.errors;
    }
    else
    {
        audec_write(AUDEC_STATUS, 0x003f0000);
        memcpy(g->pcm, dma.samples, sizeof(dma.samples));
        ++stats.granules;
        stats.input_channels = channels;
        first = RT_FALSE;
    }
    stats.last_error = rc;
    return rc;
}

static rt_err_t control(rt_device_t dev, int command, void *arg)
{
    (void)dev;
    if (!ready) return -RT_EIO;
    if (rt_interrupt_get_nest() || !rt_thread_self() || rt_critical_level() || !(PICCON & 1u))
        return -RT_EBUSY;
    if (command < AB32_AUDEC_BEGIN || command > AB32_AUDEC_GET_STATUS) return -RT_ENOSYS;
    if ((command == AB32_AUDEC_GET_STATUS || command == AB32_AUDEC_DECODE) ? !arg : !!arg)
        return -RT_EINVAL;
    if (rt_mutex_take(&lock, 0)) return -RT_EBUSY;
    rt_err_t rc = RT_EOK;
    if (command == AB32_AUDEC_GET_STATUS)
    {
        stats.active = owner != RT_NULL;
        memcpy(arg, &stats, sizeof(stats));
    }
    else if (command == AB32_AUDEC_BEGIN)
    {
        /* No current IRQ user may be displaced. This driver polls IRQ status. */
        if (owner || (PICEN & AUDEC_IRQ) || (audec_read(AUDEC_CON) & 1u)) rc = -RT_EBUSY;
        else
        {
            owner = rt_thread_self();
            saved_gate = CLKGAT0 & (AUDEC_GATE | WORK_GATE);
            saved_clock = CLKCON2 & CLOCK_MASK;
            reset_stream();
        }
    }
    else if (command == AB32_AUDEC_END && !owner) { /* idempotent */ }
    else if (owner != rt_thread_self()) rc = -RT_EBUSY;
    else if (command == AB32_AUDEC_DECODE) rc = decode(arg);
    else if (command == AB32_AUDEC_RESET) reset_stream();
    else
    {
        stop_engine();
        configure_clock(RT_FALSE);
        stats.faulted = 0;
        owner = RT_NULL;
    }
    rt_mutex_release(&lock);
    return rc;
}

static const struct rt_device_ops ops = {.control = control};
int ab32_audec_init(void)
{
    /* Fail closed on a custom/old linker script, before any peripheral writes. */
    if ((uintptr_t)work != AB32_AUDEC_WORK_BASE) return -RT_EINVAL;
    rt_err_t rc = rt_mutex_init(&lock, "audec", RT_IPC_FLAG_PRIO);
    if (rc) return rc;
    device.type = RT_Device_Class_Miscellaneous;
    device.ops = &ops;
    rc = rt_device_register(&device, "audec0", RT_DEVICE_FLAG_RDWR);
    if (rc) rt_mutex_detach(&lock);
    else
    {
        stats.work_base = AB32_AUDEC_WORK_BASE;
        stats.work_bytes = sizeof(work);
        ready = RT_TRUE;
    }
    return rc;
}
INIT_DEVICE_EXPORT(ab32_audec_init);
#endif
