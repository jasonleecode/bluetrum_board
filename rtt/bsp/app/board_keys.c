/* SPDX-License-Identifier: Apache-2.0
 * V02: S2/PF1, S3/PF0, S4/PA2, all active low. S1 is a power reset.
 * Thread-only debouncing in the existing main loop; no extra worker/IRQ.
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <finsh.h>
#include <stdlib.h>
#include <string.h>
#include "board_keys.h"
#ifdef BSP_USING_KEYS
#if defined(BSP_USING_SPI1) && (BSP_SPI1_MAPPING == 1 || BSP_SPI1_MAPPING == 5)
#error "Board keys conflict with SPI1 G1/PA2 or G5/PF0/PF1"
#endif
#if (defined(BSP_USING_I2S) && BSP_I2S_MAPPING == 0) || defined(BSP_USING_LPWM2_G3)
#error "Board KEY3 PA2 conflicts with I2S G1 or LPWM2 G3"
#endif
#define KEY_TICKS(ms) ((RT_TICK_PER_SECOND * (ms) + 999u) / 1000u)
static const rt_base_t key_pins[AB32_KEYS_COUNT] = {22, 21, 2};
static struct {
    rt_tick_t changed, down;
    rt_uint8_t candidate, pressed, long_sent;
} key_state[AB32_KEYS_COUNT];
static struct rt_device keys_device;
static struct rt_mutex keys_lock;
static struct ab32_key_event queue[AB32_KEYS_QUEUE];
static struct ab32_keys_status status;
static rt_uint8_t head, tail;
static rt_bool_t keys_ready;

static void key_push(unsigned i, rt_uint8_t kind, rt_tick_t now)
{
    if (status.queued == AB32_KEYS_QUEUE)
    {
        tail = (tail + 1) % AB32_KEYS_QUEUE;
        status.dropped++;
    }
    else status.queued++;
    queue[head] = (struct ab32_key_event){
        .tick = (rt_uint32_t)now,
        .held_ms = kind == AB32_KEY_DOWN ? 0 :
            (rt_uint32_t)((rt_uint64_t)(rt_tick_t)(now - key_state[i].down) * 1000 / RT_TICK_PER_SECOND),
        .key = i + 1, .kind = kind
    };
    head = (head + 1) % AB32_KEYS_QUEUE;
}
/* Caller holds keys_lock; separated from pin reads for deterministic tests. */
static rt_uint8_t keys_sample(const rt_ssize_t levels[AB32_KEYS_COUNT], rt_tick_t now)
{
    rt_uint8_t downs = 0;
    for (unsigned i = 0; i < AB32_KEYS_COUNT; i++)
    {
        unsigned bit = 1u << i;
        if (levels[i] != PIN_LOW && levels[i] != PIN_HIGH)
        {
            status.read_errors++;
            key_state[i].candidate = key_state[i].pressed;
            key_state[i].changed = now;
            continue;
        }
        rt_bool_t pressed = levels[i] == PIN_LOW;
        if (pressed) status.raw_mask |= bit;
        else status.raw_mask &= ~bit;
        if (pressed != key_state[i].candidate)
        {
            key_state[i].candidate = pressed;
            key_state[i].changed = now;
        }
        if (pressed != key_state[i].pressed && (rt_tick_t)(now - key_state[i].changed) >= KEY_TICKS(30))
        {
            key_state[i].pressed = pressed;
            if (pressed)
            {
                key_state[i].down = now;
                key_state[i].long_sent = 0;
                status.pressed_mask |= bit;
                key_push(i, AB32_KEY_DOWN, now);
                downs |= bit;
            }
            else
            {
                status.pressed_mask &= ~bit;
                key_push(i, AB32_KEY_UP, now);
            }
        }
        if (key_state[i].pressed && pressed && !key_state[i].long_sent &&
            (rt_tick_t)(now - key_state[i].down) >= KEY_TICKS(800))
        {
            key_state[i].long_sent = 1;
            key_push(i, AB32_KEY_LONG, now);
        }
    }
    return downs;
}
rt_uint8_t board_keys_poll(void)
{
    rt_ssize_t levels[AB32_KEYS_COUNT];
    if (!keys_ready) return 0;
    for (unsigned i = 0; i < AB32_KEYS_COUNT; i++) levels[i] = rt_pin_read(key_pins[i]);
    rt_mutex_take(&keys_lock, RT_WAITING_FOREVER);
    rt_uint8_t downs = keys_sample(levels, rt_tick_get());
    rt_mutex_release(&keys_lock);
    return downs;
}
static rt_ssize_t keys_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    (void)dev;
    rt_size_t n = 0;
    if (!keys_ready) return -RT_EIO;
    if (pos || (!buffer && size) || size % sizeof(queue[0])) return -RT_EINVAL;
    rt_mutex_take(&keys_lock, RT_WAITING_FOREVER);
    while (status.queued && n < size)
    {
        rt_memcpy((rt_uint8_t *)buffer + n, &queue[tail], sizeof(queue[0]));
        tail = (tail + 1) % AB32_KEYS_QUEUE;
        status.queued--;
        n += sizeof(queue[0]);
    }
    rt_mutex_release(&keys_lock);
    return n;
}
static rt_err_t keys_control(rt_device_t dev, int cmd, void *arg)
{
    (void)dev;
    if (!keys_ready) return -RT_EIO;
    if (cmd != AB32_KEYS_GET_STATUS) return -RT_ENOSYS;
    if (!arg) return -RT_EINVAL;
    rt_mutex_take(&keys_lock, RT_WAITING_FOREVER);
    rt_memcpy(arg, &status, sizeof(status));
    rt_mutex_release(&keys_lock);
    return RT_EOK;
}
static const struct rt_device_ops keys_ops = {.read = keys_read, .control = keys_control};
int board_keys_init(void)
{
    rt_err_t rc = rt_mutex_init(&keys_lock, "keys", RT_IPC_FLAG_PRIO);
    if (rc) return rc;
    keys_device.type = RT_Device_Class_Miscellaneous;
    keys_device.ops = &keys_ops;
    rc = rt_device_register(&keys_device, "keys0", RT_DEVICE_FLAG_RDONLY);
    if (rc) { rt_mutex_detach(&keys_lock); return rc; }
    for (unsigned i = 0; i < AB32_KEYS_COUNT; i++) rt_pin_mode(key_pins[i], PIN_MODE_INPUT_PULLUP);
    keys_ready = RT_TRUE;
    return RT_EOK;
}
INIT_APP_EXPORT(board_keys_init);

static int keys(int argc, char **argv)
{
    struct ab32_keys_status s;
    if (argc == 2 && !strcmp(argv[1], "status"))
    {
        int rc = keys_control(&keys_device, AB32_KEYS_GET_STATUS, &s);
        if (rc) return rc;
        rt_kprintf("keys0: raw=%x pressed=%x queued=%u dropped=%u read_errors=%u\n",
            s.raw_mask, s.pressed_mask, s.queued, (unsigned)s.dropped, (unsigned)s.read_errors);
        return 0;
    }
    rt_bool_t watch = (argc == 2 || argc == 3) && !strcmp(argv[1], "watch");
    if (watch || (argc == 2 && !strcmp(argv[1], "read")))
    {
        unsigned long seconds = 10;
        if (argc == 3)
        {
            char *end;
            seconds = strtoul(argv[2], &end, 10);
            if (!*argv[2] || *end || seconds < 1 || seconds > 30) return -RT_EINVAL;
        }
        rt_tick_t start = rt_tick_get();
        do
        {
            for (unsigned i = 0; i < AB32_KEYS_QUEUE; i++)
            {
                struct ab32_key_event e;
                rt_ssize_t n = keys_read(&keys_device, 0, &e, sizeof(e));
                if (n < 0) return n;
                if (!n) break;
                rt_kprintf("KEY%u %s held=%u ms tick=%u\n", e.key,
                    e.kind == AB32_KEY_DOWN ? "DOWN" : e.kind == AB32_KEY_UP ? "UP" : "LONG",
                    (unsigned)e.held_ms, (unsigned)e.tick);
            }
            if (!watch) break;
            rt_thread_mdelay(20);
        } while ((rt_tick_t)(rt_tick_get() - start) < seconds * RT_TICK_PER_SECOND);
        return 0;
    }
    rt_kprintf("keys status|read|watch [1..30 seconds]\n");
    return -RT_EINVAL;
}
MSH_CMD_EXPORT(keys, Three debounced board keys);
#endif
