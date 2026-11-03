/* SPDX-License-Identifier: Apache-2.0
 * S2 / NEC remote radio, entirely in the existing main thread.
 */
#include <rtthread.h>
#ifdef BSP_USING_RADIO_APP
#include <rtdevice.h>
#include <finsh.h>
#include <string.h>
#include "app_radio.h"
#include "board_led.h"
#include "drv_irrx.h"
#include "drv_fmrx.h"

#if !defined(BSP_USING_KEYS) || !defined(BSP_USING_LED_BREATH) || \
    !defined(BSP_USING_IRRX) || !defined(BSP_USING_FMRX)
#error "Radio app requires board keys, breathing LED, IRRX and FMRX"
#endif
#define RADIO_MIN_KHZ 87500u
#define RADIO_MAX_KHZ 108000u
#define RADIO_STEP_KHZ 100u
#define RADIO_VOLUME_STEP 5u
#define REMOTE_UP    0xb54ab34cu
#define REMOTE_DOWN  0xad52b34cu
#define REMOTE_LEFT  0xe619b34cu
#define REMOTE_RIGHT 0xbe41b34cu
#define RADIO_TICKS(ms) ((RT_TICK_PER_SECOND * (ms) + 999u) / 1000u)

static rt_device_t radio_ir;
static struct rt_mutex radio_lock;
static rt_bool_t radio_ready, radio_ir_opened;
static struct {
    rt_uint32_t frequency, volume, actions;
    rt_err_t error;
    rt_bool_t wanted, running;
} radio_state = {.frequency = 97400, .volume = 15};
static rt_uint32_t held_raw;
static rt_tick_t held_since, last_repeat, last_sync;
static int pending_command;
static rt_uint32_t pending_value;

static void radio_report(rt_err_t error)
{
    radio_state.error = error;
    rt_kprintf("radio: on=%u freq=%u kHz volume=%u%% error=%d\n",
        radio_state.running, (unsigned)radio_state.frequency,
        (unsigned)radio_state.volume, error);
}

static void radio_ir_stop(void)
{
    if (radio_ir_opened)
    {
        rt_device_control(radio_ir, AB32_IR_STOP, RT_NULL);
        rt_device_close(radio_ir);
        radio_ir_opened = RT_FALSE;
    }
    held_raw = 0;
    pending_command = 0;
}

static rt_err_t radio_start(void)
{
    struct ab32_fmrx_status fm;
    rt_err_t rc = ab32_fmrx_try_control(AB32_FMRX_STATUS, &fm);
    if (rc) return rc;
    /* Do not take over a radio session started from the diagnostic shell. */
    if (fm.running) return -RT_EBUSY;
    rt_uint32_t volume = 15, frequency = 97400;
    rc = ab32_fmrx_try_control(AB32_FMRX_VOLUME, &volume);
    if (!rc) rc = ab32_fmrx_try_control(AB32_FMRX_START, &frequency);
    if (rc) return rc;
    radio_state.running = RT_TRUE;
    radio_state.frequency = frequency;
    radio_state.volume = volume;
    rc = rt_device_open(radio_ir, RT_DEVICE_OFLAG_RDONLY);
    if (!rc)
    {
        radio_ir_opened = RT_TRUE;
        rc = rt_device_control(radio_ir, AB32_IR_START, RT_NULL);
    }
    if (!rc)
    {
        /* Discard pre-application events (START may already be active). */
        struct ab32_ir_event stale;
        for (unsigned i = 0; i < AB32_IR_QUEUE_SIZE; i++)
            if (rt_device_read(radio_ir, 0, &stale, sizeof(stale)) != sizeof(stale)) break;
        held_raw = 0;
        rc = board_led_set_active(RT_TRUE);
    }
    /* On failure the poll loop retries STOP until hardware releases FM. */
    return rc;
}

static void radio_remote(const struct ab32_ir_event *event)
{
    rt_uint32_t raw = event->raw;
    if (event->kind == AB32_IR_UP)
    {
        if (held_raw == raw) held_raw = 0;
        return;
    }
    if (raw != REMOTE_UP && raw != REMOTE_DOWN && raw != REMOTE_LEFT && raw != REMOTE_RIGHT)
        return;
    if (event->kind == AB32_IR_DOWN)
    {
        held_raw = raw;
        held_since = last_repeat = event->tick;
    }
    else if (event->kind == AB32_IR_REPEAT)
    {
        /* One station step per DOWN; volume holds repeat after 500 ms,
         * at most once per 250 ms. Never synthesize repeats without frames. */
        if (held_raw != raw || (raw != REMOTE_UP && raw != REMOTE_DOWN) ||
            (rt_tick_t)(event->tick - held_since) < RADIO_TICKS(500) ||
            (rt_tick_t)(event->tick - last_repeat) < RADIO_TICKS(250)) return;
        last_repeat = event->tick;
    }
    else return;

    rt_uint32_t value;
    int command;
    if (raw == REMOTE_UP || raw == REMOTE_DOWN)
    {
        value = radio_state.volume;
        value = raw == REMOTE_UP ? (value > 100 - RADIO_VOLUME_STEP ? 100 : value + RADIO_VOLUME_STEP)
                                : (value < RADIO_VOLUME_STEP ? 0 : value - RADIO_VOLUME_STEP);
        if (value == radio_state.volume) return;
        command = AB32_FMRX_VOLUME;
    }
    else
    {
        value = radio_state.frequency;
        if (value < RADIO_MIN_KHZ || value > RADIO_MAX_KHZ)
            value = raw == REMOTE_RIGHT ? RADIO_MIN_KHZ : RADIO_MAX_KHZ;
        else value = raw == REMOTE_RIGHT ? (value >= RADIO_MAX_KHZ ? RADIO_MIN_KHZ : value + RADIO_STEP_KHZ)
                                        : (value <= RADIO_MIN_KHZ ? RADIO_MAX_KHZ : value - RADIO_STEP_KHZ);
        command = AB32_FMRX_TUNE;
    }
    pending_command = command;
    pending_value = value;
}

static void radio_apply_command(void)
{
    if (!pending_command) return;
    rt_err_t rc = ab32_fmrx_try_control(pending_command, &pending_value);
    if (rc == -EAGAIN) return; /* keep the key action until the bus is free */
    if (!rc)
    {
        if (pending_command == AB32_FMRX_VOLUME) radio_state.volume = pending_value;
        else radio_state.frequency = pending_value;
        radio_state.actions++;
    }
    pending_command = 0;
    radio_report(rc);
}

void app_radio_init(void)
{
    radio_ir = rt_device_find("ir0");
    if (!radio_ir || !rt_device_find("fm0")) return;
    if (rt_mutex_init(&radio_lock, "radio", RT_IPC_FLAG_PRIO)) return;
    radio_ready = RT_TRUE;
}

void app_radio_poll(rt_uint8_t key_downs)
{
    if (!radio_ready || rt_mutex_take(&radio_lock, RT_WAITING_FOREVER)) return;
    if (key_downs & 1u)
    {
        radio_state.wanted = !radio_state.wanted;
        radio_state.error = RT_EOK;
    }
    if (radio_state.wanted && !radio_state.running)
    {
        rt_err_t rc = radio_start();
        if (rc != -EAGAIN)
        {
            if (rc) radio_state.wanted = RT_FALSE;
            radio_report(rc);
        }
    }
    if (radio_state.running && radio_state.wanted && !board_led_is_active())
    {
        radio_state.wanted = RT_FALSE;
        radio_state.error = -RT_EIO;
    }
    if (!radio_state.wanted && radio_state.running)
    {
        rt_err_t rc = ab32_fmrx_try_control(AB32_FMRX_STOP, RT_NULL);
        if (!rc)
        {
            radio_state.running = RT_FALSE;
            board_led_set_active(RT_FALSE);
            radio_ir_stop();
            radio_report(radio_state.error);
        }
        else if (rc != -EAGAIN) radio_report(rc);
    }
    if (radio_state.running && radio_state.wanted)
    {
        /* Resync after diagnostic-shell controls or a tune fault. A busy
         * FM mutex must not freeze key scanning or the LED envelope. */
        rt_tick_t now = rt_tick_get();
        if ((rt_tick_t)(now - last_sync) >= RADIO_TICKS(100))
        {
            struct ab32_fmrx_status fm;
            rt_err_t rc = ab32_fmrx_try_control(AB32_FMRX_STATUS, &fm);
            if (!rc)
            {
                last_sync = now;
                if (!fm.running)
                {
                    radio_state.running = radio_state.wanted = RT_FALSE;
                    board_led_set_active(RT_FALSE);
                    radio_ir_stop();
                    radio_report(fm.last_error);
                }
                else
                {
                    radio_state.frequency = fm.frequency_khz;
                    radio_state.volume = fm.volume;
                }
            }
        }
        if (radio_state.running)
        {
            /* At most one command/calibration per 10 ms main-loop turn. */
            if (!pending_command)
            {
                struct ab32_ir_event event;
                rt_ssize_t n = rt_device_read(radio_ir, 0, &event, sizeof(event));
                if (n == sizeof(event)) radio_remote(&event);
                else if (n < 0) radio_report(n);
            }
            radio_apply_command();
        }
    }
    rt_mutex_release(&radio_lock);
}

static int radio(int argc, char **argv)
{
    if (!radio_ready) return -RT_EIO;
    if (argc != 2) goto usage;
    rt_mutex_take(&radio_lock, RT_WAITING_FOREVER);
    if (!strcmp(argv[1], "start")) radio_state.wanted = RT_TRUE;
    else if (!strcmp(argv[1], "stop")) radio_state.wanted = RT_FALSE;
    else if (!strcmp(argv[1], "status"))
    {
        /* Print a copy after unlocking so main never misses S2 for printf. */
        rt_uint32_t frequency = radio_state.frequency, volume = radio_state.volume, actions = radio_state.actions;
        rt_err_t error = radio_state.error;
        rt_bool_t wanted = radio_state.wanted, running = radio_state.running;
        rt_mutex_release(&radio_lock);
        rt_kprintf("radio: wanted=%u on=%u freq=%u kHz volume=%u%% actions=%u error=%d\n",
            wanted, running, (unsigned)frequency, (unsigned)volume, (unsigned)actions, error);
        return RT_EOK;
    }
    else { rt_mutex_release(&radio_lock); goto usage; }
    radio_state.error = RT_EOK;
    rt_mutex_release(&radio_lock);
    return RT_EOK;
usage:
    rt_kprintf("radio start|stop|status; S2 toggles FM+LED, NEC arrows control volume/tuning\n");
    return -RT_EINVAL;
}
MSH_CMD_EXPORT(radio, S2 and infrared FM radio application);
#endif
