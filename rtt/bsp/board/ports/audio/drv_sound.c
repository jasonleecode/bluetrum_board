/*
 * Copyright (c) 2020-2021, Bluetrum Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Date           Author       Notes
 * 2020-12-12     greedyhao    first implementation
 */

#include <board.h>
#ifdef BSP_USING_FMRX
#include "drv_fmrx.h"
#include "fmrx_hal.h"
#endif
#ifdef BSP_USING_I2S
#include "i2s_src.h"
#ifndef BSP_I2S_MAPPING
#define BSP_I2S_MAPPING 1
#endif
#if BSP_I2S_MAPPING < 0 || BSP_I2S_MAPPING > 1
#error "I2S mapping must be 0 (PA0..4) or 1 (PE0..4)"
#endif
/* Do not let SPI1 or the existing GPIO I2C own the same pins. */
#if defined(BSP_USING_SPI1) && BSP_I2S_MAPPING == 0 && BSP_SPI1_MAPPING == 1
#error "I2S PA0..4 conflicts with SPI1 mapping 1"
#endif
#if defined(BSP_USING_I2C1) && BSP_I2S_MAPPING == 1
#error "I2S PE0..4 requires disabling the software I2C1 bus"
#endif
#endif

#define DBG_TAG              "drv.snd_dev"
#define DBG_LVL              DBG_INFO
#include <rtdbg.h>

#define SAI_AUDIO_FREQUENCY_48K         ((uint32_t)48000u)
#define SAI_AUDIO_FREQUENCY_44K         ((uint32_t)44100u)
#define SAI_AUDIO_FREQUENCY_38K         ((uint32_t)38000u)
#define TX_FIFO_SIZE                    (1024)

struct sound_device
{
    struct rt_audio_device audio;
    struct rt_audio_configure replay_config;
    rt_sem_t    semaphore;
    rt_thread_t thread;
    rt_uint8_t *tx_fifo;
    rt_uint8_t *rx_fifo;
    rt_uint8_t  volume;
    rt_uint8_t  dma_to_aubuf;
    rt_bool_t running;
};

static struct sound_device snd_dev = {0};
#ifdef BSP_USING_FMRX
static struct rt_mutex sound_lock;
static rt_bool_t pcm_open;
static struct {
    rt_err_t (*init)(rt_device_t);
    rt_err_t (*open)(rt_device_t, rt_uint16_t);
    rt_err_t (*close)(rt_device_t);
    rt_ssize_t (*write)(rt_device_t, rt_off_t, const void *, rt_size_t);
    rt_err_t (*control)(rt_device_t, int, void *);
} pcm_ops;
#endif

//apll = 采样率*ADPLL_DIV*512
//audio pll init
void adpll_init(uint8_t out_spr)
{
    PLL1CON &= ~(BIT(16) | BIT(17));                //PLL1 refclk select xosc26m
    CLKCON2  &= ~(BIT(4)| BIT(5) | BIT(6) | BIT(7));

    PLL1CON &= ~(BIT(3) | BIT(4) | BIT(5));
    PLL1CON |= BIT(3);                              //Select PLL/VCO frequency band (PLL大于206M vcos = 0x01, 否则为0)

    PLL1CON |= BIT(12);                             //enable pll1 ldo
    hal_mdelay(1);
    PLL1CON |= BIT(18);                             //pll1 sdm enable

    if (out_spr) {
        CLKCON2  |= BIT(4) | BIT(7);                //adpll_div = 10
        PLL1DIV = (245.76 * 65536) / 26;            //245.76Mhz for 48K
        // sys.aupll_type = 1;
    } else {
        CLKCON2  |= BIT(5) | BIT(7);                //adpll_div = 11
        PLL1DIV = (248.3712 * 65536) / 26;          //248.3712MHz for 44.1k
        // sys.aupll_type = 0;
    }
    hal_mdelay(1);
    PLL1CON |= BIT(20);                             //update pll1div
    PLL1CON |= BIT(6);                              //enable analog pll1
    hal_mdelay(1);                                  //wait pll1 stable
}

void dac_start(void)
{
    AUANGCON0 |= BIT(0) | BIT(1) | BIT(3); // bg ldoh bias enable

    AUANGCON0 &= ~(BIT(6)|BIT(5)|BIT(4));  // LDOH voltage select：3bit
    AUANGCON0 |= (3<<4); // 2.4/2.5/2.7/2.9/3.1/3.2

    AUANGCON0 |= BIT(2);           // LDOL enable

    AUANGCON0 |= BIT(9); //VCM enable
    AUANGCON0 &= ~(BIT(13)|BIT(12)); // VCM voltage select, 2bit
    AUANGCON0 |= (2<<12);

    AUANGCON0 |= BIT(15) | BIT(16) | BIT(17) | BIT(18); // d2a lpf audpa audpa_dly

    AUANGCON0 &= ~BIT(11); //VCM type: 0-->res divider with off-chip cap; 1-->internal VCM
    //AUANGCON0 |= BIT(11);

    AUANGCON0 &= ~BIT(19); // dac type: 0-->SC; 1-->SR
    //AUANGCON0 |= BIT(19);

    AUANGCON0 |= BIT(20); // pa type: 0-->diff; 1-->3.3V single

    AUANGCON3 &= ~(0x7<<4); //BIT[6:4]=PA_GF[2:0]
    AUANGCON3 |= (0<<4);
    AUANGCON3 &= ~(0xf); //BIT[3:0]=PA_GX[3:0]
    AUANGCON3 |= 0;

    AUANGCON3 &= ~(0xF<<8); //BIT[11:8]=PA2_GX[3:0]
    AUANGCON3 |= (0<<8);
    AUANGCON3 &= ~(0x7<<12); //BIT[14:12]=PA2_GF[2:0]
    AUANGCON3 |= (0<<12);

    AUANGCON1 |= BIT(0) | BIT(1); // dac enable: BIT(0)-->right channel; BIT(1)-->left channel
    //AUANGCON1 &= ~BIT(1); //disable left channel

    AUANGCON1 |= BIT(12); // lpf2pa enable

    AUANGCON1 &= ~BIT(29); // vcmbuf enable: 0-->disable
    //AUANGCON1 |= BIT(29);

    //AUANGCON1 |= BIT(30); // mirror enable

    //AUANGCON2 |= BIT(29) | BIT(30); // adc mute

    //AUANGCON1 |= BIT(3);  // pa mute
}

rt_section(".irq.audio")
void audio_sem_post(void)
{
    rt_sem_release(snd_dev.semaphore);
}

rt_err_t audio_sem_pend(void)
{
    return rt_sem_take(snd_dev.semaphore, rt_tick_from_millisecond(1000));
}

void saia_frequency_set(uint32_t frequency)
{
    DACDIGCON0 &= ~(0xf << 2);
    if (frequency == SAI_AUDIO_FREQUENCY_48K) {
        DACDIGCON0 |= (0 << 2);
    } else if (frequency == SAI_AUDIO_FREQUENCY_44K) {
        DACDIGCON0 |= (1 << 2);
    } else if (frequency == SAI_AUDIO_FREQUENCY_38K) {
        DACDIGCON0 |= (2 << 2);
    }
    DACDIGCON0 |= BIT(6);
}

void saia_channels_set(uint8_t channels)
{
    LOG_D("saia_channels_set=%d", channels);
    if (channels == 1) {
        AU0LMIXCOEF = 0x00007FFF;
        AU1LMIXCOEF = 0x00007FFF;
        DACDIGCON0 |= BIT(7);
        DACDIGCON0 |= BIT(8);
        AUANGCON1 &= ~BIT(0);
    } else {
        AUANGCON1 |= BIT(0);
        DACDIGCON0 &= ~BIT(7);
        DACDIGCON0 &= ~BIT(8);
    }
}

void saia_volume_set(rt_uint8_t volume)
{
    if (volume > 100)
        volume = 100;

    uint32_t dvol = volume * 327; // max is 0x7ffff
    LOG_D("dvol=0x%x", dvol);
    DACVOLCON = dvol | (0x02 << 16); // dac fade in
}

uint8_t saia_volume_get(void)
{
    return ((DACVOLCON & 0xffff) / 327);
}

static rt_err_t sound_getcaps(struct rt_audio_device *audio, struct rt_audio_caps *caps)
{
    rt_err_t result = RT_EOK;
    struct sound_device *snd_dev = RT_NULL;

    RT_ASSERT(audio != RT_NULL);
    snd_dev = (struct sound_device *)audio->parent.user_data;

    if (caps == RT_NULL)
        return -RT_EINVAL;

    switch (caps->main_type)
    {
    case AUDIO_TYPE_QUERY: /* qurey the types of hw_codec device */
    {
        switch (caps->sub_type)
        {
        case AUDIO_TYPE_QUERY:
            caps->udata.mask = AUDIO_TYPE_OUTPUT | AUDIO_TYPE_MIXER;
            break;

        default:
            result = -RT_ERROR;
            break;
        }

        break;
    }

    case AUDIO_TYPE_OUTPUT: /* Provide capabilities of OUTPUT unit */
    {
        switch (caps->sub_type)
        {
        case AUDIO_DSP_PARAM:
            caps->udata.config.samplerate   = snd_dev->replay_config.samplerate;
            caps->udata.config.channels     = snd_dev->replay_config.channels;
            caps->udata.config.samplebits   = snd_dev->replay_config.samplebits;
            break;

        case AUDIO_DSP_SAMPLERATE:
            caps->udata.config.samplerate   = snd_dev->replay_config.samplerate;
            break;

        case AUDIO_DSP_CHANNELS:
            caps->udata.config.channels     = snd_dev->replay_config.channels;
            break;

        case AUDIO_DSP_SAMPLEBITS:
            caps->udata.config.samplebits   = snd_dev->replay_config.samplebits;
            break;

        default:
            result = -RT_ERROR;
            break;
        }

        break;
    }

    case AUDIO_TYPE_MIXER: /* report the Mixer Units */
    {
        switch (caps->sub_type)
        {
        case AUDIO_MIXER_QUERY:
            caps->udata.mask = AUDIO_MIXER_VOLUME;
            break;

        case AUDIO_MIXER_VOLUME:
            caps->udata.value =  saia_volume_get();
            break;

        default:
            result = -RT_ERROR;
            break;
        }

        break;
    }

    default:
        result = -RT_ERROR;
        break;
    }

    return result;
}

static rt_err_t sound_configure_unlocked(struct rt_audio_device *audio, struct rt_audio_caps *caps)
{
    rt_err_t result = RT_EOK;
    struct sound_device *snd_dev = RT_NULL;

    RT_ASSERT(audio != RT_NULL);
    snd_dev = (struct sound_device *)audio->parent.user_data;

    if (caps == RT_NULL)
        return -RT_EINVAL;
    if (caps->main_type == AUDIO_TYPE_OUTPUT)
    {
        rt_uint32_t rate = snd_dev->replay_config.samplerate;
        rt_uint32_t channels = snd_dev->replay_config.channels;
        rt_uint32_t bits = snd_dev->replay_config.samplebits;
        if (snd_dev->running)
            return -RT_EBUSY;
        switch (caps->sub_type)
        {
        case AUDIO_DSP_PARAM:
            rate = caps->udata.config.samplerate;
            channels = caps->udata.config.channels;
            bits = caps->udata.config.samplebits;
            break;
        case AUDIO_DSP_SAMPLERATE: rate = caps->udata.config.samplerate; break;
        case AUDIO_DSP_CHANNELS: channels = caps->udata.config.channels; break;
        case AUDIO_DSP_SAMPLEBITS: bits = caps->udata.config.samplebits; break;
        default: return -RT_ENOSYS;
        }
        if ((rate != 48000 && rate != 44100) ||
            (channels != 1 && channels != 2) || bits != 16)
            return -RT_EINVAL;
#ifdef BSP_USING_I2S
        if (channels != 2) return -RT_EINVAL;
#endif
        if (rate != snd_dev->replay_config.samplerate)
            adpll_init(rate == 48000);
    }

    switch (caps->main_type)
    {
    case AUDIO_TYPE_MIXER:
    {
        switch (caps->sub_type)
        {
        case AUDIO_MIXER_VOLUME:
        {
            if (caps->udata.value < 0 || caps->udata.value > 100)
                return -RT_EINVAL;
            rt_uint8_t volume = caps->udata.value;

            saia_volume_set(volume);
            snd_dev->volume = volume;
            LOG_D("set volume %d", volume);
            break;
        }

        case AUDIO_MIXER_EXTEND:
            return -RT_ENOSYS;
        break;

        default:
            result = -RT_ERROR;
            break;
        }

        break;
    }

    case AUDIO_TYPE_OUTPUT:
    {
        switch (caps->sub_type)
        {
        case AUDIO_DSP_PARAM:
        {
            /* set samplerate */
            saia_frequency_set(caps->udata.config.samplerate);
            /* set channels */
            saia_channels_set(caps->udata.config.channels);

            /* save configs */
            snd_dev->replay_config.samplerate = caps->udata.config.samplerate;
            snd_dev->replay_config.channels   = caps->udata.config.channels;
            snd_dev->replay_config.samplebits = caps->udata.config.samplebits;
            LOG_D("set samplerate %d", snd_dev->replay_config.samplerate);
            break;
        }

        case AUDIO_DSP_SAMPLERATE:
        {
            saia_frequency_set(caps->udata.config.samplerate);
            snd_dev->replay_config.samplerate = caps->udata.config.samplerate;
            LOG_D("set samplerate %d", snd_dev->replay_config.samplerate);
            break;
        }

        case AUDIO_DSP_CHANNELS:
        {
            saia_channels_set(caps->udata.config.channels);
            snd_dev->replay_config.channels   = caps->udata.config.channels;
            LOG_D("set channels %d", snd_dev->replay_config.channels);
            break;
        }

        case AUDIO_DSP_SAMPLEBITS:
        {
            /* not support */
            snd_dev->replay_config.samplebits = caps->udata.config.samplebits;
            break;
        }

        default:
            result = -RT_ERROR;
            break;
        }

        break;
    }

    default:
        result = -RT_ENOSYS;
        break;
    }

    return result;
}

static rt_err_t sound_init_unlocked(struct rt_audio_device *audio)
{
    struct sound_device *snd_dev = RT_NULL;

    RT_ASSERT(audio != RT_NULL);
    snd_dev = (struct sound_device *)audio->parent.user_data;

    adpll_init(snd_dev->replay_config.samplerate == 48000);
    dac_start();

    /* set default params */
    saia_frequency_set(snd_dev->replay_config.samplerate);
    saia_channels_set(snd_dev->replay_config.channels);
    saia_volume_set(snd_dev->volume);

    return RT_EOK;
}

static rt_err_t sound_start_unlocked(struct rt_audio_device *audio, int stream)
{
    struct sound_device *snd_dev = RT_NULL;

    RT_ASSERT(audio != RT_NULL);
    snd_dev = (struct sound_device *)audio->parent.user_data;

    if (stream == AUDIO_STREAM_REPLAY)
    {
        LOG_D("open sound device");

        AUBUFSIZE       = (TX_FIFO_SIZE / 4 - 1);
        AUBUFSIZE       |= (TX_FIFO_SIZE / 8) << 16;
        AUBUFSTARTADDR  = DMA_ADR(snd_dev->rx_fifo);

        DACDIGCON0  |= BIT(0) | BIT(10); // (0x01<<2)

        AUBUFCON |= BIT(1);
        snd_dev->running = RT_TRUE;
#ifdef BSP_USING_I2S
        if (!snd_dev->dma_to_aubuf)
        {
            hal_rcu_periph_clk_enable(RCU_IIS);
            i2s_src_init(BSP_I2S_MAPPING);
        }
#endif
    }
    else
        return -RT_ENOSYS;

    return RT_EOK;
}

static rt_err_t sound_stop_unlocked(struct rt_audio_device *audio, int stream)
{
    RT_ASSERT(audio != RT_NULL);

    if (stream == AUDIO_STREAM_REPLAY)
    {
        LOG_D("close sound device");
        snd_dev.running = RT_FALSE;
#ifdef BSP_USING_I2S
        if (!snd_dev.dma_to_aubuf)
        {
            i2s_src_exit(BSP_I2S_MAPPING);
            DACDIGCON0 &= ~BIT(23); /* release the SRC route selected by init */
            hal_rcu_periph_clk_disable(RCU_IIS);
        }
#endif
        AUBUFCON &= ~BIT(4);
        DACDIGCON0 &= ~(BIT(0) | BIT(10));
        rt_sem_release(snd_dev.semaphore);
    }
    else
        return -RT_ENOSYS;

    return RT_EOK;
}

rt_ssize_t sound_transmit(struct rt_audio_device *audio, const void *writeBuf, void *readBuf, rt_size_t size)
{
    struct sound_device *snd_dev = RT_NULL;
    rt_size_t tmp_size = size / 4;
    rt_size_t count = 0;

    RT_ASSERT(audio != RT_NULL);
    snd_dev = (struct sound_device *)audio->parent.user_data;

    if (!writeBuf || ((rt_ubase_t)writeBuf & 3) || (size & 3) ||
        !snd_dev->running || snd_dev->dma_to_aubuf)
        return 0;
    while (tmp_size-- > 0 && snd_dev->running) {
        while ((AUBUFCON & BIT(8)) && snd_dev->running) { // aubuf full
            AUBUFCON |= BIT(1) | BIT(4);
            if (audio_sem_pend() != RT_EOK)
                return count ? (rt_ssize_t)(count * 4) : -RT_ETIMEOUT;
        }
        if (!snd_dev->running)
            break;
        AUBUFDATA = ((const uint32_t *)writeBuf)[count++];
    }

    return count * 4;
}

static void sound_buffer_info(struct rt_audio_device *audio, struct rt_audio_buf_info *info)
{
    struct sound_device *snd_dev = RT_NULL;

    RT_ASSERT(audio != RT_NULL);
    snd_dev = (struct sound_device *)audio->parent.user_data;

    /**
     *               TX_FIFO
     * +----------------+----------------+
     * |     block1     |     block2     |
     * +----------------+----------------+
     *  \  block_size  /
     */
    info->buffer      = snd_dev->tx_fifo;
    info->total_size  = TX_FIFO_SIZE;
    info->block_size  = TX_FIFO_SIZE / 2;
    info->block_count = 2;
}

/* Share DAC ownership with FM. Hold the mutex across PLL setup (which can
 * sleep), so a concurrent sound0 open/start/configure cannot overwrite FM.
 * transmit stays unlocked: FM cannot acquire while PCM replay is running.
 */
#ifdef BSP_USING_FMRX
#define SOUND_CALL(expression) do { \
    rt_err_t ret = rt_mutex_take(&sound_lock, RT_WAITING_FOREVER); \
    if (ret != RT_EOK) return ret; \
    ret = snd_dev.dma_to_aubuf ? -RT_EBUSY : (expression); \
    rt_mutex_release(&sound_lock); \
    return ret; \
} while (0)
#else
#define SOUND_CALL(expression) return (expression)
#endif

static rt_err_t sound_configure(struct rt_audio_device *audio, struct rt_audio_caps *caps)
{ SOUND_CALL(sound_configure_unlocked(audio, caps)); }
static rt_err_t sound_init(struct rt_audio_device *audio)
{ SOUND_CALL(sound_init_unlocked(audio)); }
static rt_err_t sound_start(struct rt_audio_device *audio, int stream)
{ SOUND_CALL(sound_start_unlocked(audio, stream)); }
static rt_err_t sound_stop(struct rt_audio_device *audio, int stream)
{ SOUND_CALL(sound_stop_unlocked(audio, stream)); }

#ifdef BSP_USING_FMRX
rt_err_t ab32_sound_fm_acquire(rt_uint32_t *buffer_words)
{
    rt_err_t ret;
    if (!buffer_words || !snd_dev.rx_fifo)
        return -RT_EINVAL;
    ret = rt_mutex_take(&sound_lock, RT_WAITING_FOREVER);
    if (ret != RT_EOK)
        return ret;
    if (snd_dev.running || snd_dev.dma_to_aubuf || pcm_open || snd_dev.audio.parent.ref_count)
        ret = -RT_EBUSY;
    else
    {
        snd_dev.dma_to_aubuf = RT_TRUE;
        rt_memset(snd_dev.rx_fifo, 0, TX_FIFO_SIZE);
        /* Same 38 kHz FM DAC selection as the SDK fmrx_test1.c example.
         * Keep the user's PCM config; restore it when FM releases the DAC.
         */
        sound_init_unlocked(&snd_dev.audio);
        saia_frequency_set(SAI_AUDIO_FREQUENCY_38K);
        saia_channels_set(2);
        saia_volume_set(0);
        sound_start_unlocked(&snd_dev.audio, AUDIO_STREAM_REPLAY);
        *buffer_words = TX_FIFO_SIZE / 4;
    }
    rt_mutex_release(&sound_lock);
    return ret;
}

void ab32_sound_fm_release(void)
{
    rt_mutex_take(&sound_lock, RT_WAITING_FOREVER);
    if (snd_dev.dma_to_aubuf)
    {
        saia_volume_set(0);
        sound_stop_unlocked(&snd_dev.audio, AUDIO_STREAM_REPLAY);
        fmrx_dac_sync_reset();
        saia_frequency_set(snd_dev.replay_config.samplerate);
        saia_channels_set(snd_dev.replay_config.channels);
        saia_volume_set(snd_dev.volume);
        snd_dev.dma_to_aubuf = RT_FALSE;
    }
    rt_mutex_release(&sound_lock);
}

void ab32_sound_fm_volume(rt_uint8_t volume)
{
    rt_mutex_take(&sound_lock, RT_WAITING_FOREVER);
    if (snd_dev.dma_to_aubuf)
        saia_volume_set(volume);
    rt_mutex_release(&sound_lock);
}

/* Gate the audio framework before it allocates/queues PCM. The framework's
 * start path marks replay active even when the HAL rejects a start, so HAL
 * checks alone are not enough. Never hold sound_lock while close/STOP waits
 * for the audio worker to call sound_stop(). pcm_open keeps FM excluded then.
 */
static rt_err_t sound_device_init(rt_device_t dev)
{ SOUND_CALL(pcm_ops.init ? pcm_ops.init(dev) : RT_EOK); }

static rt_err_t sound_device_open(rt_device_t dev, rt_uint16_t flags)
{
    rt_err_t ret = rt_mutex_take(&sound_lock, RT_WAITING_FOREVER);
    if (ret != RT_EOK) return ret;
    ret = snd_dev.dma_to_aubuf ? -RT_EBUSY : pcm_ops.open(dev, flags);
    if (ret == RT_EOK) pcm_open = RT_TRUE;
    rt_mutex_release(&sound_lock);
    return ret;
}

static rt_err_t sound_device_close(rt_device_t dev)
{
    rt_err_t ret = pcm_ops.close(dev);
    rt_mutex_take(&sound_lock, RT_WAITING_FOREVER);
    if (ret == RT_EOK) pcm_open = RT_FALSE;
    rt_mutex_release(&sound_lock);
    return ret;
}

static rt_ssize_t sound_device_write(rt_device_t dev, rt_off_t pos,
                                     const void *buffer, rt_size_t size)
{
    /* Normal writes have an open PCM owner, which cannot be taken by FM. */
    if (snd_dev.dma_to_aubuf || !pcm_open) return -RT_EBUSY;
    return pcm_ops.write(dev, pos, buffer, size);
}

static rt_err_t sound_device_control(rt_device_t dev, int cmd, void *args)
{
    rt_err_t ret = rt_mutex_take(&sound_lock, RT_WAITING_FOREVER);
    if (ret != RT_EOK) return ret;
    /* Require an open PCM owner for controls, including START and STOP. */
    ret = snd_dev.dma_to_aubuf || !pcm_open ? -RT_EBUSY : RT_EOK;
    rt_mutex_release(&sound_lock);
    if (ret != RT_EOK) return ret;
    return pcm_ops.control(dev, cmd, args);
}

#ifdef RT_USING_DEVICE_OPS
static const struct rt_device_ops sound_device_ops = {
    .init = sound_device_init, .open = sound_device_open,
    .close = sound_device_close, .write = sound_device_write,
    .control = sound_device_control
};
#endif

static void sound_install_fm_gate(void)
{
    rt_device_t dev = &snd_dev.audio.parent;
#ifdef RT_USING_DEVICE_OPS
    pcm_ops.init = dev->ops->init;
    pcm_ops.open = dev->ops->open;
    pcm_ops.close = dev->ops->close;
    pcm_ops.write = dev->ops->write;
    pcm_ops.control = dev->ops->control;
    dev->ops = &sound_device_ops;
#else
    pcm_ops.init = dev->init;
    pcm_ops.open = dev->open;
    pcm_ops.close = dev->close;
    pcm_ops.write = dev->write;
    pcm_ops.control = dev->control;
    dev->init = sound_device_init;
    dev->open = sound_device_open;
    dev->close = sound_device_close;
    dev->write = sound_device_write;
    dev->control = sound_device_control;
#endif
}
#endif

static struct rt_audio_ops ops =
{
    .getcaps     = sound_getcaps,
    .configure   = sound_configure,
    .init        = sound_init,
    .start       = sound_start,
    .stop        = sound_stop,
    .transmit    = sound_transmit,
    .buffer_info = sound_buffer_info,
};

rt_section(".irq.audio")
static void audio_isr(int vector, void *param)
{
    rt_interrupt_enter();

    //Audio buffer pend
    if (AUBUFCON & BIT(5)) {
        AUBUFCON |= BIT(1);         //Audio Buffer Pend Clear
        AUBUFCON &= ~BIT(4);
        audio_sem_post();
    }
    rt_interrupt_leave();
}

static void audio_thread_entry(void *parameter)
{
    while (1)
    {
        if ((snd_dev.dma_to_aubuf == RT_FALSE) && (snd_dev.audio.replay != RT_NULL) && (snd_dev.running == RT_TRUE) && (snd_dev.audio.replay->activated == RT_TRUE)) {
            rt_audio_tx_complete(&snd_dev.audio);
        } else {
            rt_thread_mdelay(50);
        }
    }
}

static int rt_hw_sound_init(void)
{
    rt_err_t result = -RT_ENOMEM;
#ifdef BSP_USING_FMRX
    result = rt_mutex_init(&sound_lock, "sndlock", RT_IPC_FLAG_PRIO);
    if (result != RT_EOK)
        return result;
    result = -RT_ENOMEM;
#endif
    snd_dev.tx_fifo = rt_calloc(1, TX_FIFO_SIZE);
    snd_dev.rx_fifo = rt_calloc(1, TX_FIFO_SIZE);
    if (!snd_dev.tx_fifo || !snd_dev.rx_fifo)
        goto fail;
    snd_dev.semaphore = rt_sem_create("snd", 0, RT_IPC_FLAG_FIFO);
    if (!snd_dev.semaphore)
        goto fail;
    snd_dev.thread = rt_thread_create("audio", audio_thread_entry, RT_NULL,
                                      1024, 20, 1);
    if (!snd_dev.thread)
        goto fail;
    snd_dev.replay_config.samplerate = SAI_AUDIO_FREQUENCY_48K;
    snd_dev.replay_config.channels = 2;
    snd_dev.replay_config.samplebits = 16;
    snd_dev.volume = 55;
    snd_dev.audio.ops = &ops;
    result = rt_audio_register(&snd_dev.audio, "sound0", RT_DEVICE_FLAG_WRONLY, &snd_dev);
    if (result != RT_EOK)
        goto fail;
#ifdef BSP_USING_FMRX
    sound_install_fm_gate();
#endif
    rt_hw_interrupt_install(IRQ_AUBUF0_1_VECTOR, audio_isr, RT_NULL, "au_isr");
    return rt_thread_startup(snd_dev.thread);
fail:
    if (snd_dev.thread) rt_thread_delete(snd_dev.thread);
    if (snd_dev.semaphore) rt_sem_delete(snd_dev.semaphore);
    if (snd_dev.rx_fifo) rt_free(snd_dev.rx_fifo);
    if (snd_dev.tx_fifo) rt_free(snd_dev.tx_fifo);
    rt_memset(&snd_dev, 0, sizeof(snd_dev));
#ifdef BSP_USING_FMRX
    rt_mutex_detach(&sound_lock);
#endif
    return result;
}
INIT_DEVICE_EXPORT(rt_hw_sound_init);
