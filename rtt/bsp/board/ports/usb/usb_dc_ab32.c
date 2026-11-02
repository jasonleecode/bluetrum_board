/* SPDX-License-Identifier: Apache-2.0
 * AB32 DMA-backed USB device controller, derived from the recovered
 * usb_comm/usb_device_ep/usb_device_enum register sequences, not a vendor .a.
 * The register file resembles MUSB but has 32-bit spaced registers and an
 * AB32 DMA front-end. Generic MUSB FIFO accesses are NOT interchangeable.
 * All APIs/callbacks run on one polling thread; USB IRQ (PIC bit 7) stays off.
 */
#include "usbd_core.h"
#include "usb_ab32.h"
#ifndef AB32_USB_REG
#define AB32_USB_REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
#endif
#ifndef AB32_USB_DELAY
extern void delay_us(uint32_t us);
#define AB32_USB_DELAY(us) delay_us(us)
#endif
#define R(a) AB32_USB_REG(a)
#define EP_COUNT 4
#define MPS 64
struct endpoint {
    uint8_t *buffer;
    uint32_t length, done;
    uint16_t mps, packet;
    uint8_t type;
    bool open, busy, stalled;
};
static struct endpoint in_ep[EP_COUNT], out_ep[EP_COUNT];
static USB_MEM_ALIGNX uint8_t ep0_dma[MPS];
static USB_MEM_ALIGNX uint8_t in_dma[EP_COUNT - 1][MPS];
static USB_MEM_ALIGNX uint8_t out_dma[EP_COUNT - 1][MPS];
static struct usb_setup_packet setup;
static uint8_t address;
static bool address_pending, active;
static uint32_t ep0_sent, saved_de, saved_clock, saved_gate, saved_mux;
struct ab32_usb_stats ab32_usb_stats;

static bool valid_ep(uint8_t ep) { return !(ep & 0x70) && (ep & 15) < EP_COUNT; }
static struct endpoint *endpoint(uint8_t ep)
{ return ep & 0x80 ? &in_ep[ep & 15] : &out_ep[ep & 15]; }
bool ab32_usb_ep_ready(uint8_t ep)
{
    if (!valid_ep(ep)) return false;
    struct endpoint *e = endpoint(ep);
    return active && e->open && !e->stalled && !e->busy;
}
static void select_ep(unsigned ep) { R(0x238) = ep; }
static unsigned rx_count(unsigned ep)
{ return R(0x258) | (ep ? R(0x25c) << 8 : 0); }
static void clear_transfers(void)
{
    memset(in_ep, 0, sizeof(in_ep)); memset(out_ep, 0, sizeof(out_ep));
    address_pending = false; ep0_sent = 0; memset(&setup, 0, sizeof(setup));
}
static void arm_out(unsigned ep)
{
    R(0x310 + ep * 8) = (uintptr_t)out_dma[ep - 1];
    R(0x250) = 0x10;
}
static void send_packet(unsigned ep)
{
    struct endpoint *e = &in_ep[ep];
    uint32_t n = MIN(e->length - e->done, e->mps);
    uint8_t *dma = ep ? in_dma[ep - 1] : ep0_dma;
    if (n) memcpy(dma, e->buffer + e->done, n);
    e->packet = n;
    if (ep) R(0x314 + ep * 8) = (uintptr_t)dma;
    __sync_synchronize(); /* SRAM data must precede the DMA doorbell. */
    R(0x308) = (1u << (ep + 16)) | n;
    if (ep) {
        R(0x244) = 1;
        AB32_USB_DELAY(2);
    } else {
        /* Do not end a full packet prematurely when the Host expects a ZLP. */
        bool end = n < e->mps || ep0_sent + n == setup.wLength;
        R(0x244) = 2 | (end ? 8 : 0);
    }
}
int usb_dc_init(uint8_t busid)
{
    if (busid || active) return -1;
    /* Current BSP derives 48 MHz from its 240 MHz PLL0. Do not retune a PLL
     * shared by the CPU/Bluetooth to make USB appear to start successfully. */
    if (R(0x38c) != 240u * 65536u / 26u) return -2;
    saved_de = R(0x650) & 0x18;
    saved_clock = R(0x3bc) & (7u << 16);
    saved_gate = R(0x3b0) & 0x4000;
    saved_mux = R(0x074) & 0x400;
    R(0x538) = 0x80;
    R(0x650) |= 0x18;
    R(0x074) &= ~0x400u;
    R(0x3bc) = (R(0x3bc) & ~(7u << 16)) | (4u << 16);
    R(0x3b0) |= 0x4000;
    R(0x300) = 0x20; R(0x304) = 0; R(0x308) = 0; R(0x30c) = 0;
    AB32_USB_DELAY(5);
    R(0x300) = 4; R(0x300) |= 0x30; R(0x300) |= 1;
    R(0x314) = (uintptr_t)ep0_dma;
    clear_transfers();
    R(0x21c) = 1; R(0x220) = 0; R(0x224) = 0; R(0x228) = 0;
    R(0x22c) = 7; /* RESET, RESUME, SUSPEND; status reads acknowledge events. */
    (void)R(0x208); (void)R(0x20c); (void)R(0x210); (void)R(0x214); (void)R(0x218);
    R(0x200) = 0x80;
    R(0x30c) = 15;
    R(0x304) = 0x50000; /* Device mode and pull-up: usb_device_enter(). */
    R(0x300) |= 2;
    active = true;
    usbd_event_reset_handler(0);
    return 0;
}
int usb_dc_deinit(uint8_t busid)
{
    if (busid) return -1;
    if (!active) return 0;
    R(0x538) = 0x80;
    R(0x300) = 0x20; R(0x304) = 0; R(0x308) = 0; R(0x30c) = 0;
    R(0x21c) = 0; R(0x224) = 0; R(0x22c) = 0;
    clear_transfers(); active = false;
    R(0x650) = (R(0x650) & ~0x18u) | saved_de;
    R(0x3bc) = (R(0x3bc) & ~(7u << 16)) | saved_clock;
    R(0x3b0) = (R(0x3b0) & ~0x4000u) | saved_gate;
    R(0x074) = (R(0x074) & ~0x400u) | saved_mux;
    return 0;
}
int usbd_set_address(uint8_t busid, uint8_t addr)
{
    if (busid || addr > 127) return -1;
    address = addr;
    address_pending = setup.bmRequestType == 0 && setup.bRequest == USB_REQUEST_SET_ADDRESS;
    if (!address_pending) R(0x200) = addr | 0x80u;
    return 0;
}
int usbd_set_remote_wakeup(uint8_t busid) { return -1; }
uint8_t usbd_get_port_speed(uint8_t busid) { return USB_SPEED_FULL; }
int usbd_ep_open(uint8_t busid, const struct usb_endpoint_descriptor *d)
{
    if (busid || !d || !valid_ep(d->bEndpointAddress)) return -1;
    unsigned ep = d->bEndpointAddress & 15;
    unsigned mps = d->wMaxPacketSize;
    unsigned type = d->bmAttributes & 3;
    if (!mps || mps > MPS || (ep == 0 ? type != 0 || mps != MPS : (type != 2 && type != 3))) return -1;
    struct endpoint *e = endpoint(d->bEndpointAddress);
    memset(e, 0, sizeof(*e)); e->mps = mps; e->type = type; e->open = true;
    select_ep(ep);
    if (ep && (d->bEndpointAddress & 0x80)) {
        R(0x244) = 0x48; R(0x248) = type == 3 ? 0x40 : 0;
        R(0x240) = (mps + 7) >> 3;
        R(0x21c) |= 1u << ep;
    } else if (ep) {
        R(0x250) = 0x90; R(0x254) = type == 3 ? 0x40 : 0;
        R(0x24c) = (mps + 7) >> 3;
        R(0x310 + ep * 8) = (uintptr_t)out_dma[ep - 1];
        R(0x224) |= 1u << ep;
    }
    return 0;
}
int usbd_ep_close(uint8_t busid, uint8_t ep)
{
    if (busid || !valid_ep(ep)) return -1;
    unsigned n = ep & 15;
    select_ep(n);
    if (n) {
        if (ep & 0x80) { R(0x21c) &= ~(1u << n); R(0x244) = 0x48; }
        else { R(0x224) &= ~(1u << n); R(0x250) = 0x90; }
    }
    memset(endpoint(ep), 0, sizeof(struct endpoint));
    return 0;
}
int usbd_ep_set_stall(uint8_t busid, uint8_t ep)
{
    if (busid || !valid_ep(ep)) return -1;
    struct endpoint *e = endpoint(ep);
    e->busy = false; e->stalled = true;
    select_ep(ep & 15);
    if (!(ep & 15)) {
        in_ep[0].busy = out_ep[0].busy = false;
        address_pending = false;
        R(0x244) = 0x60;
    } else if (ep & 0x80) R(0x244) = 0x18; /* flush and stall, never spin */
    else R(0x250) = 0x21;
    return 0;
}
int usbd_ep_clear_stall(uint8_t busid, uint8_t ep)
{
    if (busid || !valid_ep(ep)) return -1;
    endpoint(ep)->stalled = false;
    select_ep(ep & 15);
    if (!(ep & 15)) R(0x244) = 0;
    else if (ep & 0x80) R(0x244) = 0x48;
    else R(0x250) = 0x90;
    return 0;
}
int usbd_ep_is_stalled(uint8_t busid, uint8_t ep, uint8_t *stalled)
{
    if (busid || !valid_ep(ep) || !stalled) return -1;
    *stalled = endpoint(ep)->stalled;
    return 0;
}
int usbd_ep_start_write(uint8_t busid, uint8_t ep, const uint8_t *data, uint32_t len)
{
    if (busid || !valid_ep(ep) || !(ep & 0x80) || (!data && len)) return -1;
    struct endpoint *e = endpoint(ep);
    if (!e->open || e->stalled || e->busy) return -2;
    select_ep(ep & 15);
    if (R(0x244) & ((ep & 15) ? 1u : 2u)) return -3;
    e->buffer = (uint8_t *)data; e->length = len; e->done = 0; e->busy = true;
    send_packet(ep & 15);
    return 0;
}
int usbd_ep_start_read(uint8_t busid, uint8_t ep, uint8_t *data, uint32_t len)
{
    if (busid || !valid_ep(ep) || (ep & 0x80) || (!data && len)) return -1;
    struct endpoint *e = endpoint(ep);
    if (!e->open || e->stalled || e->busy) return -2;
    e->buffer = data; e->length = len; e->done = 0; e->busy = true;
    select_ep(ep);
    if (ep) arm_out(ep);
    return 0;
}
static void in_complete(unsigned ep)
{
    struct endpoint *e = &in_ep[ep];
    if (!e->busy || (R(0x244) & (ep ? 1u : 2u))) return;
    e->done += e->packet; ++ab32_usb_stats.in_packets;
    if (!ep) ep0_sent += e->packet;
    if (ep && e->done < e->length) { send_packet(ep); return; }
    /* Core manages EP0 packet slicing; application owns bulk slicing. */
    uint32_t done = e->done;
    e->busy = false;
    if (!ep && address_pending) {
        R(0x200) = address | 0x80u;
        address_pending = false;
    }
    usbd_event_ep_in_complete_handler(0, ep | 0x80, done);
}
static void out_complete(unsigned ep)
{
    struct endpoint *e = &out_ep[ep];
    if (!e->busy) return; /* keep NAK/backpressure until a read is posted */
    unsigned n = rx_count(ep);
    if (n > MPS || n > e->length - e->done ||
        (!ep && n != MIN(e->length - e->done, e->mps))) {
        ++ab32_usb_stats.errors;
        usbd_ep_set_stall(0, ep); return;
    }
    __sync_synchronize(); /* Observe completed DMA before reading the buffer. */
    if (n) memcpy(e->buffer + e->done, ep ? out_dma[ep - 1] : ep0_dma, n);
    if (!ep) { for (unsigned i = 0; i < n; ++i) (void)R(0x280); }
    e->done += n; ++ab32_usb_stats.out_packets;
    bool done = !ep || n < e->mps || e->done == e->length;
    if (!ep) R(0x244) = e->length ? 0x40 : 0x48;
    if (done) {
        uint32_t total = e->done; e->busy = false;
        usbd_event_ep_out_complete_handler(0, ep, total);
    } else arm_out(ep);
}
void ab32_usb_poll(void)
{
    if (!active) return;
    uint32_t bus = R(0x218) & R(0x22c);
    uint32_t tx = R(0x208) & R(0x21c);
    uint32_t rx = R(0x210) & R(0x224);
    if (bus & 4) {
        R(0x300) &= ~6u; R(0x300) |= 4;
        AB32_USB_DELAY(5); clear_transfers();
        R(0x314) = (uintptr_t)ep0_dma;
        R(0x21c) = 1; R(0x224) = 0;
        R(0x300) |= 2;
        ++ab32_usb_stats.resets;
        usbd_event_reset_handler(0);
        return;
    }
    if (bus & 1) usbd_event_suspend_handler(0);
    if (bus & 2) usbd_event_resume_handler(0);
    select_ep(0);
    uint32_t csr = R(0x244);
    if (csr & (4 | 16)) {
        if (csr & 4) R(0x244) = 0;
        if (csr & 16) R(0x244) = 0x80;
        in_ep[0].busy = out_ep[0].busy = false;
        address_pending = false;
    }
    /* IN ACK and OUT status may arrive before the next polling tick. */
    if ((csr & 1) && !(csr & 2) && (tx & 1) && in_ep[0].busy && rx_count(0) == 0)
        in_complete(0);
    if (csr & 1) {
        /* A new SETUP aborts the old transfer; after DATAEND hardware may
         * consume status without a separate RX interrupt. */
        if (!out_ep[0].busy || (out_ep[0].length == 0 && rx_count(0) == 8)) {
            unsigned n = rx_count(0);
            if (n != 8) { ++ab32_usb_stats.errors; usbd_ep_set_stall(0, 0); }
            else {
                memcpy(&setup, ep0_dma, 8);
                for (unsigned i = 0; i < 8; ++i) (void)R(0x280);
                in_ep[0].busy = out_ep[0].busy = false;
                in_ep[0].stalled = out_ep[0].stalled = false;
                address_pending = false; ep0_sent = 0;
                R(0x244) = 0x40;
                ++ab32_usb_stats.setups;
                usbd_event_ep0_setup_complete_handler(0, (uint8_t *)&setup);
            }
        } else out_complete(0);
    } else if (tx & 1) in_complete(0);
    if (!usb_device_is_configured(0)) return;
    for (unsigned ep = 1; ep < EP_COUNT; ++ep) {
        select_ep(ep);
        if (tx & (1u << ep)) in_complete(ep);
        select_ep(ep); /* callback may change selected endpoint */
        if ((rx & (1u << ep)) || (out_ep[ep].busy && (R(0x250) & 1))) {
            if (R(0x250) & 1) out_complete(ep);
        }
    }
}
