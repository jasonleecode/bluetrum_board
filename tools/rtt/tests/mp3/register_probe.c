/* Bounded MP3 AUDEC datapath probe. Test profile only; 0x54000..0x57000
 * must be reserved by the linker. No vendor codec object runs on the board. */
#include <board.h>
#include <stdint.h>
#include <string.h>
#include "mp3_vectors.h"
#define REG(a) (*(volatile uint32_t *)(a))
#define MC REG(0x180)
#define MK REG(0x184)
#define MS REG(0x188)
#define MO REG(0x190)
static uint32_t pcm[578] __attribute__((aligned(16)));
static void dump(unsigned n)
{
    static const char hex[] = "0123456789abcdef";
    char line[65];
    unsigned i,j;
    const unsigned char *p=(const unsigned char *)&pcm[1];
    rt_kprintf("PCM %u 2304\n",n);
    for(i=0;i<2304;i+=32) {
        for(j=0;j<32;j++) { line[j*2]=hex[p[i+j]>>4];line[j*2+1]=hex[p[i+j]&15]; }
        line[64]=0;rt_kprintf("%s\n",line);
    }
}
static int mp3hw(void)
{
    uint32_t gate=CLKGAT0, irq=PICEN, clock=CLKCON2;
    unsigned n,i; int rc=0;
    if ((uintptr_t)&pcm[0]>=0x50000) return -RT_ERROR;
    rt_kprintf("MP3HW begin clock=%08x gate=%08x con=%08x status=%08x\n",clock,gate,MC,MS);
    PICENCLR=1u<<IRQ_AUDEC_VECTOR;
    /* Existing BSP divider keeps AUDEC <48 MHz. Enable its gate and RAM1. */
    CLKGAT0 |= (1u<<RCU_AUDEC) | (1u<<RCU_RAM3);
    CLKCON2=(clock & ~(15u<<13)) | (3u<<13);
    MC=0x08000000;MK=0;MS=0x003f3f00;
    memset((void *)0x54000,0,0x3000);
    for(n=0;n<MP3_VECTOR_COUNT;n++) {
        const struct mp3_vector *v=&mp3_vectors[n];
        memset((void *)0x55000,0,0x900);
        memset((void *)0x56b00,0,0x9c);
        for(i=v->first;i<v->first+v->count;i++) REG(0x54000+mp3_words[i].offset)=mp3_words[i].value;
        for(i=0;i<578;i++)pcm[i]=0x5aa55aa5;
        MS=0x003f0000;
        MO=(uintptr_t)&pcm[1];
        /* Bit 27 is reset, not part of the steady decode configuration. */
        MC=(v->control & ~(1u<<27))|(1u<<26);
        /* CRWN does not implement RISC-V FENCE; SRAM/MMIO are uncached. */
        __asm__ volatile("" ::: "memory");
        rt_tick_t begin=rt_tick_get();
        MK=v->kick;
        while(!(MS&0x20) && (rt_tick_t)(rt_tick_get()-begin)<RT_TICK_PER_SECOND/5) rt_thread_mdelay(1);
        uint32_t st=MS;
        /* CRWN does not implement RISC-V FENCE; SRAM/MMIO are uncached. */
        __asm__ volatile("" ::: "memory");
        unsigned changed=0;for(i=1;i<=576;i++)changed+=pcm[i]!=0x5aa55aa5;
        rt_kprintf("GRANULE %u status=%08x con=%08x ticks=%u changed=%u guards=%u\n",n,st,MC,rt_tick_get()-begin,changed,pcm[0]==0x5aa55aa5 && pcm[577]==0x5aa55aa5);
        if(!(st&0x20) || changed!=576 || pcm[0]!=0x5aa55aa5 || pcm[577]!=0x5aa55aa5) {rc=-RT_ERROR;break;}
        MS=0x003f0000;
        dump(n);
    }
    MC=0x08000000;MK=0;MS=0x003f3f00;
    CLKGAT0=gate;CLKCON2=clock;
    if(irq&(1u<<IRQ_AUDEC_VECTOR))PICENSET=1u<<IRQ_AUDEC_VECTOR;
    rt_kprintf("MP3HW end rc=%d granules=%u\n",rc,n);
    return rc;
}
MSH_CMD_EXPORT(mp3hw, Validate MP3 hardware datapath with known vectors);
