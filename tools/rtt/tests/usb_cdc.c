/* Real CherryUSB core + CDC + AB32 DCD, modeled register banks and DMA. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static uint32_t regs[0x800 / 4], banks[4][32];
static uint32_t *mmio(unsigned address)
{
    if (address >= 0x240 && address < 0x280) return &banks[regs[0x238/4] & 3][(address-0x240)/4];
    assert(address < sizeof(regs)); return &regs[address / 4];
}
#define AB32_USB_REG(a) (*mmio(a))
#define AB32_USB_DELAY(us) ((void)(us))
#include "../../../rtt/bsp/board/ports/usb/usb_dc_ab32.c"
#include "../../../rtt/bsp/board/ports/usb/app_usb_cdc.c"
int rt_kprintf(const char *fmt, ...) { return 0; }
void rt_assert_handler(const char *e, const char *fn, rt_size_t line)
{ fprintf(stderr,"%s:%lu %s\n",fn,(unsigned long)line,e); abort(); }
static void tick(unsigned tx, unsigned rx, unsigned bus)
{
    regs[0x208/4]=tx; regs[0x210/4]=rx; regs[0x218/4]=bus;
    poll_once();
    regs[0x208/4]=regs[0x210/4]=regs[0x218/4]=0;
}
static void setup_request(uint8_t type, uint8_t req, uint16_t val, uint16_t index, uint16_t length)
{
    struct usb_setup_packet s={type,req,val,index,length};
    memcpy(ep0_dma,&s,8); banks[0][(0x258-0x240)/4]=8;
    /* SETUPEND aborts an outstanding control transfer. */
    banks[0][1]=1 | ((in_ep[0].busy || out_ep[0].busy) ? 16 : 0);
    tick(1,0,0);
}
static void complete_in(unsigned ep)
{ banks[ep][1]=0; tick(1u<<ep,0,0); }
static void complete_out(unsigned ep, const void *p, unsigned n)
{
    if (n && n<=64) memcpy(ep ? out_dma[ep-1] : ep0_dma,p,n);
    banks[ep][6]=n & 255; banks[ep][7]=n>>8;
    banks[ep][ep?4:1]=1;
    tick(ep?0:1,1u<<ep,0);
}
static void configure(void)
{
    setup_request(0,USB_REQUEST_SET_ADDRESS,7,0,0);
    assert(address_pending && regs[0x200/4]==0x80);
    complete_in(0); assert(regs[0x200/4]==0x87 && !address_pending);
    setup_request(0,USB_REQUEST_SET_CONFIGURATION,1,0,0);
    assert(configured && usb_device_is_configured(0));
    complete_in(0);
}
int main(void)
{
    assert(initialize()==-2); /* wrong PLL must not touch the USB hardware */
    assert(!active && regs[0x300/4]==0);
    regs[0x38c/4]=240u*65536u/26u;
    assert(!initialize() && active);
    tick(0,0,4); assert(ab32_usb_stats.resets==1 && !configured);
    setup_request(0x80,USB_REQUEST_GET_DESCRIPTOR,0x100,0,18);
    assert(in_ep[0].packet==18 && ep0_dma[0]==18 && ep0_dma[1]==1);
    assert(!memcmp(ep0_dma,device_desc,18)); complete_in(0);
    complete_out(0,NULL,0);
    setup_request(0x80,USB_REQUEST_GET_DESCRIPTOR,0x100,0,18);
    complete_out(0,NULL,0); /* IN ACK and OUT status in the same polling tick */
    assert(!in_ep[0].busy && !out_ep[0].busy && !in_ep[0].stalled);
    setup_request(0x80,USB_REQUEST_GET_DESCRIPTOR,0x200,0,255);
    assert(in_ep[0].packet==64 && !memcmp(ep0_dma,config_desc,64));
    complete_in(0);
    assert(in_ep[0].packet==sizeof(config_desc)-64);
    assert(!memcmp(ep0_dma,config_desc+64,sizeof(config_desc)-64));
    complete_in(0); complete_out(0,NULL,0);
    setup_request(0x80,USB_REQUEST_GET_DESCRIPTOR,0x600,0,10);
    assert(in_ep[0].stalled); /* no false high-speed qualifier */
    configure();
    assert(!in_ep[0].stalled && out_ep[2].busy && in_ep[1].open);
    setup_request(0x21,CDC_REQUEST_SET_CONTROL_LINE_STATE,3,0,0);
    assert(dtr); complete_in(0);
    uint8_t coding[7]={0x80,0x25,0,0,0,0,8};
    setup_request(0x21,CDC_REQUEST_SET_LINE_CODING,0,0,7);
    complete_out(0,coding,7); complete_in(0);
    assert(line_coding.dwDTERate==9600);
    setup_request(0xa1,CDC_REQUEST_GET_LINE_CODING,0,0,7);
    assert(!memcmp(ep0_dma,coding,7)); complete_in(0); complete_out(0,NULL,0);
    /* Strict request lengths prevent upstream's variable-size stack memcpy. */
    uint8_t bad[8]={0};
    setup_request(0x21,CDC_REQUEST_SET_LINE_CODING,0,0,8);
    complete_out(0,bad,8); assert(in_ep[0].stalled);
    setup_request(0xa1,CDC_REQUEST_GET_LINE_CODING,0,1,7);
    assert(in_ep[0].stalled); /* data interface has no class control handler */
    uint8_t bytes[64]; for(unsigned i=0;i<64;++i) bytes[i]=i;
    for(unsigned n=0;n<=64;++n) {
        assert(out_ep[2].busy);
        complete_out(2,bytes,n);
        assert(in_ep[1].busy && !out_ep[2].busy);
        assert(!memcmp(in_dma[0],bytes,n));
        assert(usbd_ep_start_write(0,0x81,bytes,n)<0);
        complete_in(1);
        if(n==64) { assert(in_ep[1].busy && in_ep[1].packet==0); complete_in(1); }
        assert(out_ep[2].busy);
    }
    assert(rx_bytes==2080 && tx_bytes==2080 && !io_errors);
    /* Oversize OUT does not overwrite user buffers; recovery requires reset. */
    complete_out(2,bytes,65); assert(out_ep[2].stalled && ab32_usb_stats.errors==1);
    setup_request(2,USB_REQUEST_CLEAR_FEATURE,0,2,0);
    assert(!out_ep[2].stalled && out_ep[2].busy); complete_in(0);
    complete_out(2,bytes,1); complete_in(1);
    assert(rx_bytes==2081 && tx_bytes==2081);
    tick(0,0,4); assert(!configured && !dtr && !in_ep[1].open);
    configure();
    complete_out(2,bytes,64); assert(in_ep[1].busy);
    tick(0,0,4); assert(!in_ep[1].busy && !out_ep[2].busy);
    configure();
    setup_request(0,USB_REQUEST_SET_CONFIGURATION,0,0,0);
    assert(!usb_device_is_configured(0) && !configured && !out_ep[2].busy); complete_in(0);
    unsigned before=rx_bytes; complete_out(2,bytes,1); assert(rx_bytes==before);
    assert(usbd_ep_start_read(1,2,bytes,64)<0);
    assert(usbd_ep_start_write(0,0x84,bytes,64)<0);
    assert(usbd_ep_start_read(0,0x82,bytes,64)<0);
    setup_request(0,USB_REQUEST_SET_ADDRESS,0,0,0);
    assert(regs[0x200/4]==0x87 && address_pending);
    complete_in(0); assert(regs[0x200/4]==0x80 && !address_pending);
    assert(!usbd_deinitialize(0) && !active && !configured);
    assert(regs[0x650/4]==0 && regs[0x3b0/4]==0 && regs[0x304/4]==0);
    assert(!initialize()); /* same objects reusable after stop */
    tick(0,0,4); configure();
    puts("PASS: real USB core/CDC + AB32 DCD: descriptors, deferred address, config, line coding, STALL recovery, echo 0..64, ZLP/backpressure, overflow, reset/restart");
}
