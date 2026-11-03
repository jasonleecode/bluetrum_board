/* Real driver under staged completion, DMA faults and competing callers. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "board.h"
#define AB32_AUDEC_TEST 1
#include "../../../rtt/bsp/libraries/hal_drivers/drv_audec.c"
volatile rt_uint32_t audec_system[5];
static struct rt_thread threads[2];
static unsigned who, irq_nest, critical, irq_locked, mutex_locked, reg_fail;
static unsigned polls, writes, kicks, stall, corrupt, running;
static unsigned init_fail, take_fail;
static rt_tick_t tick;
static rt_uint32_t hw[4], submitted_control;
static rt_uint32_t input[576], band[39], right_input[576], right_band[39], output[578];
static rt_err_t command(int c, void *a) { return device.ops->control(&device,c,a); }
rt_thread_t rt_thread_self(void) { return who < 2 ? &threads[who] : RT_NULL; }
rt_uint8_t rt_interrupt_get_nest(void) { return irq_nest; }
rt_uint16_t rt_critical_level(void) { return critical; }
rt_base_t rt_hw_interrupt_disable(void) { assert(!irq_locked);irq_locked=1;return 1; }
void rt_hw_interrupt_enable(rt_base_t level) { assert(level==1 && irq_locked);irq_locked=0; }
rt_err_t rt_mutex_init(rt_mutex_t m,const char *n,rt_uint8_t f)
{ assert(m==&lock && !strcmp(n,"audec") && f==RT_IPC_FLAG_PRIO); return init_fail ? -RT_ERROR:0; }
rt_err_t rt_mutex_detach(rt_mutex_t m) { assert(m==&lock);return 0; }
rt_err_t rt_mutex_take(rt_mutex_t m,rt_int32_t timeout)
{ assert(m==&lock && !timeout);if(take_fail)return -RT_ETIMEOUT;assert(!mutex_locked);mutex_locked=1;m->owner=rt_thread_self();return 0; }
rt_err_t rt_mutex_release(rt_mutex_t m)
{ assert(m==&lock && mutex_locked);mutex_locked=0;m->owner=0;return 0; }
rt_err_t rt_device_register(rt_device_t d,const char *n,rt_uint16_t flags)
{ assert(d==&device && !strcmp(n,"audec0") && flags==RT_DEVICE_FLAG_RDWR);return reg_fail ? -RT_ERROR:0; }
rt_tick_t rt_tick_get(void) { return tick; }
rt_tick_t rt_tick_from_millisecond(rt_int32_t ms) { return ms; }
rt_err_t rt_thread_mdelay(rt_int32_t ms)
{
    assert(ms==1 && mutex_locked && !irq_locked && ab32_audec_power_busy());
    tick++;polls++;
    if(stall)return 0;
    hw[2]=0x0f; /* Intermediate completion is not sufficient. */
    if(polls==2) {
        for(unsigned i=0;i<576;i++)dma.samples[i]=0x2000+i;
        if(corrupt)dma.after=0;
        hw[2]=0x3f;running=0;
    }
    return 0;
}
rt_uint32_t audec_read(rt_uint32_t a)
{ assert(a>=0x180 && a<=0x190);return hw[a==0x190 ? 3:(a-0x180)/4]; }
void audec_write(rt_uint32_t a,rt_uint32_t v)
{
    assert(mutex_locked);writes++;
    if(a==0x180) { hw[0]=v&~(1u<<27);if(v&(1u<<27)){running=0;hw[2]=0;} }
    else if(a==0x184) {
        hw[1]=v;
        if(v){assert(v==0x00a0003f);submitted_control=hw[0];polls=0;kicks++;running=1;}
    }
    else if(a==0x188)hw[2]=0;
    else {assert(a==0x190);hw[3]=v;}
}
int main(void)
{
#ifdef BAD_AUDEC_LAYOUT
    assert(ab32_audec_init()==-RT_EINVAL && !ready && !writes);
    puts("PASS: invalid AUDEC linker layout rejected before MMIO");
    return 0;
#endif
    PICCON=1;PICEN=1u<<IRQ_TMR0_VECTOR;CLKGAT0=0xffffffff;CLKCON2=0xabcdef12;TMR0CON=0x81;
    init_fail=1;assert(ab32_audec_init()==-RT_ERROR && !ready && !writes);init_fail=0;
    reg_fail=1;assert(ab32_audec_init()==-RT_ERROR && !ready && !writes);reg_fail=0;
    assert(ab32_audec_init()==0 && ready && !writes);
    struct ab32_audec_status s;
    assert(command(AB32_AUDEC_GET_STATUS,&s)==0 && !s.active && s.work_base==0x54000);
    assert(command(AB32_AUDEC_RESET,0)==-RT_EBUSY);
    assert(command(AB32_AUDEC_END,0)==0);
    assert(command(999,0)==-RT_ENOSYS);
    irq_nest=1;assert(command(AB32_AUDEC_BEGIN,0)==-RT_EBUSY);irq_nest=0;
    critical=1;assert(command(AB32_AUDEC_BEGIN,0)==-RT_EBUSY);critical=0;
    PICCON=0;assert(command(AB32_AUDEC_BEGIN,0)==-RT_EBUSY);PICCON=1;
    who=2;assert(command(AB32_AUDEC_BEGIN,0)==-RT_EBUSY);who=0;
    take_fail=1;assert(command(AB32_AUDEC_BEGIN,0)==-RT_EBUSY);take_fail=0;
    PICEN|=AUDEC_IRQ;assert(command(AB32_AUDEC_BEGIN,0)==-RT_EBUSY);PICEN&=~AUDEC_IRQ;
    hw[0]=1;assert(command(AB32_AUDEC_BEGIN,0)==-RT_EBUSY);hw[0]=0;
    assert(!writes);
    unsigned original_gate=CLKGAT0,original_clock=CLKCON2,original_irq=PICEN;
    memset(work,0x55,sizeof(work));
    assert(command(AB32_AUDEC_BEGIN,0)==0 && ab32_audec_power_busy());
    for(unsigned i=0;i<sizeof(work);i++)assert(work[i]==0);
    assert(command(AB32_AUDEC_BEGIN,0)==-RT_EBUSY);
    struct ab32_audec_granule g={.spectrum=input,.bands=band,.pcm=&output[1],.pcm_bytes=2304,.timeout_ms=5};
    who=1;
    assert(command(AB32_AUDEC_DECODE,&g)==-RT_EBUSY);
    assert(command(AB32_AUDEC_RESET,0)==-RT_EBUSY);
    assert(command(AB32_AUDEC_END,0)==-RT_EBUSY);
    assert(command(AB32_AUDEC_GET_STATUS,&s)==0 && s.active);who=0;
    g.block_type=4;assert(command(AB32_AUDEC_DECODE,&g)==-RT_EINVAL);g.block_type=0;
    g.timeout_ms=1001;assert(command(AB32_AUDEC_DECODE,&g)==-RT_EINVAL);g.timeout_ms=5;
    g.pcm_bytes=2303;assert(command(AB32_AUDEC_DECODE,&g)==-RT_EINVAL);g.pcm_bytes=2304;
    g.spectrum=0;assert(command(AB32_AUDEC_DECODE,&g)==-RT_EINVAL);
    g.spectrum=(void*)0x55000;assert(command(AB32_AUDEC_DECODE,&g)==-RT_EINVAL);
    g.spectrum=(void*)((char*)input+1);assert(command(AB32_AUDEC_DECODE,&g)==-RT_EINVAL);g.spectrum=input;
    g.pcm=dma.samples;assert(command(AB32_AUDEC_DECODE,&g)==-RT_EINVAL);g.pcm=&output[1];
    PICEN=0;assert(command(AB32_AUDEC_DECODE,&g)==-RT_EBUSY);PICEN=original_irq;
    assert(kicks==0);
    for(unsigned i=0;i<578;i++)output[i]=GUARD;
    input[7]=123;band[5]=456;work[10]=42;
    tick=(rt_tick_t)-1;
    assert(command(AB32_AUDEC_DECODE,&g)==0 && polls==2 && (submitted_control&(1u<<19)));
    assert(output[0]==GUARD && output[577]==GUARD);
    for(unsigned i=0;i<576;i++)assert(output[i+1]==0x2000+i);
    assert(((rt_uint32_t*)(work+0x1000))[7]==123 && ((rt_uint32_t*)(work+0x2b00))[5]==456);
    assert(command(AB32_AUDEC_DECODE,&g)==0 && !(submitted_control&(1u<<19)) && work[10]==42);
    assert(command(AB32_AUDEC_RESET,0)==0 && work[10]==0);
    assert(command(AB32_AUDEC_DECODE,&g)==0 && (submitted_control&(1u<<19)));
    for(unsigned i=0;i<578;i++)output[i]=GUARD;
    stall=1;
    assert(command(AB32_AUDEC_DECODE,&g)==-RT_ETIMEOUT && !running && !(CLKGAT0&AUDEC_GATE));
    for(unsigned i=0;i<578;i++)assert(output[i]==GUARD);
    assert(command(AB32_AUDEC_GET_STATUS,&s)==0 && s.timeouts==1 && s.faulted && s.last_ticks==5);
    assert(command(AB32_AUDEC_DECODE,&g)==-RT_EIO);
    stall=0;assert(command(AB32_AUDEC_RESET,0)==0);
    corrupt=1;assert(command(AB32_AUDEC_DECODE,&g)==-RT_EIO && !running);
    for(unsigned i=0;i<578;i++)assert(output[i]==GUARD);
    corrupt=0;assert(command(AB32_AUDEC_RESET,0)==0);
    assert(command(AB32_AUDEC_DECODE,&g)==0);
    /* Simulate unrelated users changing clock fields during this session. */
    CLKGAT0^=1u<<7;CLKCON2^=1u<<4;
    assert(command(AB32_AUDEC_END,0)==0 && !ab32_audec_power_busy());
    assert(CLKGAT0==(original_gate^(1u<<7)) && CLKCON2==(original_clock^(1u<<4)) && PICEN==original_irq);
    assert(command(AB32_AUDEC_GET_STATUS,&s)==0 && !s.active && s.errors==2 && s.granules==4);
    assert(command(AB32_AUDEC_BEGIN,0)==0 && command(AB32_AUDEC_END,0)==0);
    assert(command(AB32_AUDEC_BEGIN,0)==0);
    unsigned before_writes=writes;
    g.mode=3;assert(command(AB32_AUDEC_DECODE,&g)==-RT_EINVAL);
    g.mode=AB32_AUDEC_STEREO;assert(command(AB32_AUDEC_DECODE,&g)==-RT_EINVAL);
    g.spectrum_right=right_input;assert(command(AB32_AUDEC_DECODE,&g)==-RT_EINVAL);
    g.bands_right=(void*)0x56b9c;assert(command(AB32_AUDEC_DECODE,&g)==-RT_EINVAL);
    g.bands_right=(void*)((char*)right_band+1);assert(command(AB32_AUDEC_DECODE,&g)==-RT_EINVAL);
    g.bands_right=right_band;g.block_type_right=4;
    assert(command(AB32_AUDEC_DECODE,&g)==-RT_EINVAL);
    g.block_type_right=3;g.spectrum_right=dma.samples;
    assert(command(AB32_AUDEC_DECODE,&g)==-RT_EINVAL);
    g.spectrum_right=right_input;
    g.mode=AB32_AUDEC_MONO;assert(command(AB32_AUDEC_DECODE,&g)==-RT_EINVAL);
    assert(writes==before_writes); /* bad inputs never alter histories or MMIO */
    g.mode=AB32_AUDEC_STEREO;g.block_type=1;
    input[0]=0x1234;right_input[0]=0x5678;right_input[575]=0xabcd;right_band[38]=0x789a;
    assert(command(AB32_AUDEC_DECODE,&g)==0);
    assert(submitted_control==((1u<<26)|(1u<<19)|0x19015));
    assert(!memcmp(work+0x1000,input,sizeof(input)) && !memcmp(work+0x2b00,band,sizeof(band)));
    assert(!memcmp(work+0x1d80,right_input,sizeof(right_input)) && !memcmp(work+0x2b9c,right_band,sizeof(right_band)));
    assert(stats.input_channels==2);
    work[20]=0x5a;g.mode=AB32_AUDEC_MS_STEREO;g.block_type=2;g.block_type_right=0;
    assert(command(AB32_AUDEC_DECODE,&g)==0 && submitted_control==((1u<<26)|0x2915) && work[20]==0x5a);
    g.mode=AB32_AUDEC_MONO;g.spectrum_right=g.bands_right=0;
    before_writes=writes;assert(command(AB32_AUDEC_DECODE,&g)==-RT_EINVAL && writes==before_writes);
    assert(command(AB32_AUDEC_RESET,0)==0 && stats.input_channels==0 && work[20]==0);
    assert(command(AB32_AUDEC_DECODE,&g)==0 && stats.input_channels==1);
    assert(command(AB32_AUDEC_END,0)==0);
    puts("PASS: stereo/M-S control fields, independent channels/blocks, invalid right inputs, channel history transitions");
    puts("PASS: AUDEC ownership, validation, staged DMA, wrap-safe timeout, fault recovery, PM observer and masked clock restore");
}
