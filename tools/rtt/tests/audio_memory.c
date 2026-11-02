/* Run the actual framework initializer with allocation and HAL failures.
 * Verify that a subsequent open/init can retry without leaked pools/queues.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../rtt/components/drivers/audio/dev_audio.c"
static int live, allocations, fail_at, mutexes, pipes, hardware_error, pipe_error;
void rt_assert_handler(const char *e, const char *f, rt_size_t l) { abort(); }
int rt_kprintf(const char *fmt, ...) { return 0; }
void *rt_malloc(rt_size_t size)
{
    if (++allocations == fail_at) return NULL;
    void *p = malloc(size); assert(p); live++; return p;
}
void rt_free(void *p) { assert(p && live > 0); live--; free(p); }
void *rt_memset(void *p, int c, rt_ubase_t n) { return memset(p, c, n); }
rt_mp_t rt_mp_create(const char *n, rt_size_t count, rt_size_t size)
{ return rt_malloc(sizeof(struct rt_mempool)); }
rt_err_t rt_mp_delete(rt_mp_t p) { rt_free(p); return RT_EOK; }
rt_err_t rt_data_queue_init(struct rt_data_queue *q, rt_uint16_t n, rt_uint16_t lwm,
                            void (*notify)(struct rt_data_queue *, rt_uint32_t))
{ q->queue = rt_malloc(n * (sizeof(void *) + sizeof(rt_size_t))); return q->queue ? RT_EOK : -RT_ENOMEM; }
rt_err_t rt_data_queue_deinit(struct rt_data_queue *q)
{ rt_free(q->queue); return RT_EOK; }
rt_err_t rt_mutex_init(rt_mutex_t m, const char *n, rt_uint8_t f) { mutexes++; return RT_EOK; }
rt_err_t rt_mutex_detach(rt_mutex_t m) { assert(mutexes > 0); mutexes--; return RT_EOK; }
rt_err_t rt_audio_pipe_init(struct rt_audio_pipe *p, const char *n, rt_int32_t f,
                             rt_uint8_t *buf, rt_size_t size)
{ if (pipe_error) return -RT_ERROR; p->ringbuffer.buffer_ptr = buf; pipes++; return RT_EOK; }
rt_err_t rt_audio_pipe_detach(struct rt_audio_pipe *p) { assert(pipes > 0); pipes--; return RT_EOK; }
static rt_err_t hardware_init(struct rt_audio_device *a) { return hardware_error ? -RT_EIO : RT_EOK; }
static void buffer_info(struct rt_audio_device *a, struct rt_audio_buf_info *i)
{ assert(a->replay); i->total_size = 1024; }
static struct rt_audio_ops ops = {.init = hardware_init, .buffer_info = buffer_info};
static void cleanup(struct rt_audio_device *a)
{
    if (a->record) {
        rt_audio_pipe_detach(&a->record->pipe);
        rt_free(a->record->pipe.ringbuffer.buffer_ptr); rt_free(a->record);
    }
    if (a->replay) {
        rt_mutex_detach(&a->replay->lock); rt_data_queue_deinit(&a->replay->queue);
        rt_mp_delete(a->replay->mp); rt_free(a->replay);
    }
    a->replay = NULL; a->record = NULL;
    assert(!live && !mutexes && !pipes);
}
int main(void)
{
    struct rt_audio_device a = {.ops = &ops};
    a.parent.flag = RT_DEVICE_FLAG_RDWR;
    /* replay object, pool, queue, record object, record buffer */
    for (int failure = 1; failure <= 5; ++failure) {
        fail_at = failure; allocations = 0;
        assert(_audio_dev_init(&a.parent) == -RT_ENOMEM);
        assert(!live && !mutexes && !pipes && !a.replay && !a.record);
        fail_at = 0;
        assert(_audio_dev_init(&a.parent) == RT_EOK);
        assert(a.replay->buf_info.total_size == 1024);
        cleanup(&a);
    }
    pipe_error = 1;
    assert(_audio_dev_init(&a.parent) == -RT_ERROR);
    assert(!live && !mutexes && !pipes && !a.replay && !a.record);
    pipe_error = 0; hardware_error = 1;
    assert(_audio_dev_init(&a.parent) == -RT_EIO);
    assert(!live && !mutexes && !pipes && !a.replay && !a.record);
    hardware_error = 0;
    a.parent.flag = RT_DEVICE_FLAG_RDONLY;
    assert(_audio_dev_init(&a.parent) == RT_EOK && !a.replay && a.record);
    cleanup(&a);
    puts("PASS: PCM allocation/HAL/pipe failure rollback, retry, record-only init");
}
