/* Exercise the actual application against FM/IR/LED device models. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../../rtt/bsp/app/app_radio.c"

static struct rt_device fake_ir, fake_fm;
static struct ab32_fmrx_status model_fm;
static struct ab32_ir_event next_event;
static rt_tick_t tick;
static int locked, led_on, ir_on, have_event, starts, stops, writes;
static int busy_command, fail_command, fail_led, fail_ir, fail_open, ir_refs;
static rt_err_t failure;

rt_tick_t rt_tick_get(void) { return tick; }
rt_device_t rt_device_find(const char *name)
{ return !strcmp(name,"fm0") ? &fake_fm : !strcmp(name,"ir0") ? &fake_ir : NULL; }
rt_err_t rt_mutex_init(rt_mutex_t m, const char *name, rt_uint8_t flag) { return 0; }
rt_err_t rt_mutex_take(rt_mutex_t m, rt_int32_t timeout) { assert(!locked); locked=1; return 0; }
rt_err_t rt_mutex_release(rt_mutex_t m) { assert(locked); locked=0; return 0; }
int rt_kprintf(const char *fmt, ...) { return 0; }
rt_bool_t board_led_is_active(void) { return led_on; }
rt_err_t board_led_set_active(rt_bool_t active)
{ if (active && fail_led) return -RT_EIO; led_on=active; return 0; }
rt_err_t rt_device_open(rt_device_t dev, rt_uint16_t flags)
{ assert(dev==&fake_ir && flags==RT_DEVICE_OFLAG_RDONLY); if(fail_open) return -RT_EIO; ir_refs++; return 0; }
rt_err_t rt_device_close(rt_device_t dev)
{ assert(dev==&fake_ir && ir_refs>0); ir_refs--; return 0; }
rt_err_t rt_device_control(rt_device_t dev, int command, void *arg)
{
    assert(dev == &fake_ir && locked);
    if (command == AB32_IR_START) { if(fail_ir) return -RT_EIO; ir_on=1; }
    else { assert(command == AB32_IR_STOP); ir_on=0; }
    return 0;
}
rt_ssize_t rt_device_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    assert(dev == &fake_ir && pos == 0 && size == sizeof(next_event) && locked && ir_refs==1);
    if (!have_event) return 0;
    memcpy(buffer,&next_event,size); have_event=0; return size;
}
rt_err_t ab32_fmrx_try_control(int command, void *arg)
{
    assert(locked);
    if (command == busy_command) return -EAGAIN;
    if (command == fail_command)
    {
        if (command == AB32_FMRX_TUNE && failure == -RT_ETIMEOUT) model_fm.running=0;
        model_fm.last_error=failure;
        return failure;
    }
    if (command == AB32_FMRX_STATUS) { *(struct ab32_fmrx_status *)arg=model_fm; return 0; }
    writes++;
    if (command == AB32_FMRX_START)
    {
        assert(!model_fm.running && model_fm.volume==15);
        model_fm.running=1; model_fm.frequency_khz=*(rt_uint32_t*)arg; starts++;
    }
    else if (command == AB32_FMRX_STOP) { model_fm.running=0; stops++; }
    else if (command == AB32_FMRX_VOLUME)
    { assert(*(rt_uint32_t*)arg<=100); model_fm.volume=*(rt_uint32_t*)arg; }
    else { assert(command==AB32_FMRX_TUNE && model_fm.running); model_fm.frequency_khz=*(rt_uint32_t*)arg; }
    model_fm.last_error=0;
    return 0;
}
static void reset(void)
{
    memset(&radio_state,0,sizeof(radio_state));
    memset(&model_fm,0,sizeof(model_fm)); model_fm.volume=55;
    radio_state.frequency=97400; radio_state.volume=15;
    radio_ready=radio_ir_opened=0; held_raw=0; last_sync=0; pending_command=0;
    tick=10000; locked=led_on=ir_on=have_event=starts=stops=writes=0;
    busy_command=fail_command=fail_led=fail_ir=fail_open=ir_refs=0;
    app_radio_init(); assert(radio_ready && !writes && !led_on && !ir_on);
}
static void poll(unsigned downs) { app_radio_poll(downs); assert(!locked); tick+=10; }
static void key(rt_uint32_t raw, unsigned kind)
{ next_event=(struct ab32_ir_event){.raw=raw,.tick=tick,.kind=kind}; have_event=1; poll(0); }
static void frequency(unsigned khz) { model_fm.frequency_khz=radio_state.frequency=khz; }
static void volume(unsigned percent) { model_fm.volume=radio_state.volume=percent; }
static void on(void)
{ poll(1); assert(radio_state.running && led_on && ir_on && starts==1); }

int main(void)
{
    reset(); poll(0); poll(2); assert(!starts);
    on(); assert(model_fm.frequency_khz==97400 && model_fm.volume==15);
    for(unsigned i=0;i<100;i++) poll(0);
    assert(starts==1); /* a held board key is not another DOWN */
    key(REMOTE_UP,AB32_IR_DOWN); assert(model_fm.volume==20);
    key(REMOTE_UP,AB32_IR_UP); assert(!held_raw);
    key(REMOTE_DOWN,AB32_IR_DOWN); assert(model_fm.volume==15);
    key(REMOTE_RIGHT,AB32_IR_DOWN); assert(model_fm.frequency_khz==97500);
    key(REMOTE_LEFT,AB32_IR_DOWN); assert(model_fm.frequency_khz==97400);
    unsigned actions=radio_state.actions;
    key(REMOTE_LEFT,AB32_IR_REPEAT); key(REMOTE_UP^1,AB32_IR_DOWN);
    assert(radio_state.actions==actions);
    volume(98); key(REMOTE_UP,AB32_IR_DOWN); assert(model_fm.volume==100);
    int before=writes; key(REMOTE_UP,AB32_IR_DOWN); assert(writes==before);
    volume(2); key(REMOTE_DOWN,AB32_IR_DOWN); assert(model_fm.volume==0);
    before=writes; key(REMOTE_DOWN,AB32_IR_DOWN); assert(writes==before);
    frequency(108000); key(REMOTE_RIGHT,AB32_IR_DOWN); assert(model_fm.frequency_khz==87500);
    key(REMOTE_LEFT,AB32_IR_DOWN); assert(model_fm.frequency_khz==108000);
    frequency(76000); key(REMOTE_RIGHT,AB32_IR_DOWN); assert(model_fm.frequency_khz==87500);
    volume(15); tick=(rt_tick_t)-400;
    key(REMOTE_UP,AB32_IR_DOWN); assert(model_fm.volume==20);
    tick+=140; key(REMOTE_UP,AB32_IR_REPEAT); assert(model_fm.volume==20);
    tick+=350; key(REMOTE_UP,AB32_IR_REPEAT); assert(model_fm.volume==25);
    tick+=100; key(REMOTE_UP,AB32_IR_REPEAT); assert(model_fm.volume==25);
    tick+=150; key(REMOTE_UP,AB32_IR_REPEAT); assert(model_fm.volume==30);
    key(REMOTE_UP,AB32_IR_UP); tick+=500;
    key(REMOTE_UP,AB32_IR_REPEAT); assert(model_fm.volume==30);

    busy_command=AB32_FMRX_VOLUME;
    key(REMOTE_DOWN,AB32_IR_DOWN); assert(model_fm.volume==30 && pending_command);
    busy_command=0; poll(0); assert(model_fm.volume==25 && !pending_command);
    busy_command=AB32_FMRX_STOP;
    poll(1); assert(!radio_state.wanted && radio_state.running && led_on);
    busy_command=0; poll(0); assert(!radio_state.running && !led_on && !ir_on && !ir_refs);
    have_event=1; /* old queued key must not change startup defaults */
    poll(1); assert(model_fm.volume==15 && model_fm.frequency_khz==97400 && !have_event);
    puts("PASS: S2 on/off/defaults, exact remote codes, 100 kHz wrap, volume clamp/hold/tick wrap, busy retry");

    reset(); busy_command=AB32_FMRX_START; poll(1);
    assert(radio_state.wanted && !radio_state.running && !led_on);
    busy_command=0; poll(0); assert(radio_state.running && led_on);
    reset(); fail_command=AB32_FMRX_START; failure=-RT_EBUSY; poll(1);
    assert(!radio_state.wanted && !radio_state.running && !led_on && !ir_on && radio_state.error==-RT_EBUSY);
    reset(); model_fm.running=1; poll(1);
    assert(model_fm.running && !radio_state.running && !stops && radio_state.error==-RT_EBUSY);
    reset(); fail_led=1; poll(1);
    assert(starts==1 && stops==1 && !model_fm.running && !ir_on && radio_state.error==-RT_EIO);
    reset(); fail_ir=1; poll(1);
    assert(starts==1 && stops==1 && !led_on && !model_fm.running && !ir_refs && radio_state.error==-RT_EIO);
    reset(); fail_open=1; poll(1);
    assert(starts==1 && stops==1 && !model_fm.running && !ir_refs && radio_state.error==-RT_EIO);
    reset(); on(); fail_command=AB32_FMRX_TUNE; failure=-RT_ETIMEOUT;
    key(REMOTE_RIGHT,AB32_IR_DOWN); tick+=100; poll(0);
    assert(!radio_state.running && !radio_state.wanted && !led_on && !ir_on && radio_state.error==-RT_ETIMEOUT);
    reset(); on(); led_on=0; poll(0);
    assert(!radio_state.running && stops==1 && radio_state.error==-RT_EIO);
    reset(); on(); model_fm.running=0; tick+=100; poll(0);
    assert(!radio_state.running && !led_on && !ir_on);
    reset();
    char *start[]={"radio","start"}, *stop[]={"radio","stop"}, *status[]={"radio","status"};
    assert(radio(2,start)==0 && !model_fm.running); poll(0); assert(model_fm.running);
    assert(radio(2,status)==0 && !locked);
    assert(radio(2,stop)==0); poll(0); assert(!model_fm.running);
    puts("PASS: startup rollback, RF busy, LED/IR faults, failed retune, diagnostic stop resync, shell serialization");
}
