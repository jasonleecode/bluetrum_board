/* SPDX-License-Identifier: Apache-2.0
 * CDC ACM binary echo bring-up. One USB worker owns core, DCD and buffers.
 * UART0 stays the shell/programming channel; CDC does not take over FinSH.
 */
#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "usb_ab32.h"
#include <finsh.h>
#define CDC_IN 0x81
#define CDC_OUT 0x02
#define CDC_NOTIFY 0x83
/* Private bench identifier only, not an allocated production VID/PID. */
static const uint8_t device_desc[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xef, 2, 1, 0xffff, 0xab32, 0x0100, 1)
};
static const uint8_t config_desc[] = {
    USB_CONFIG_DESCRIPTOR_INIT(9 + CDC_ACM_DESCRIPTOR_LEN, 2, 1, USB_CONFIG_BUS_POWERED, 100),
    CDC_ACM_DESCRIPTOR_INIT(0, CDC_NOTIFY, CDC_OUT, CDC_IN, 64, 2)
};
static const uint8_t *get_device(uint8_t speed) { return device_desc; }
static const uint8_t *get_config(uint8_t speed) { return config_desc; }
static const char *get_string(uint8_t speed, uint8_t index)
{
    static const char *const strings[] = {
        "\x09\x04", "AB32 RTT", "AB32 CDC", "AB32-CDC-0001"
    };
    return index < 4 ? strings[index] : NULL;
}
static const struct usb_descriptor descriptors = {
    .device_descriptor_callback = get_device,
    .config_descriptor_callback = get_config,
    .string_descriptor_callback = get_string
};
static struct usbd_interface control_intf, data_intf;
static usbd_request_handler original_cdc_handler;
static USB_MEM_ALIGNX uint8_t echo_buffer[64];
static struct cdc_line_coding line_coding = {115200, 0, 0, 8};
static rt_thread_t worker;
static volatile uint8_t request;
static volatile bool running, configured, dtr;
static volatile int last_error;
static uint32_t rx_bytes, tx_bytes, io_errors;

/* Bound upstream v1.5.2 SET_LINE_CODING memcpy and validate class routing. */
static int cdc_request(uint8_t bus, struct usb_setup_packet *s, uint8_t **data, uint32_t *len)
{
    if (s->wIndex != 0) return -1;
    switch (s->bRequest) {
    case CDC_REQUEST_SET_LINE_CODING:
        if (s->bmRequestType != 0x21 || s->wLength != 7 || s->wValue || *len != 7) return -1;
        if ((*data)[4] > 2 || (*data)[5] > 4 ||
            !((*data)[6] == 5 || (*data)[6] == 6 || (*data)[6] == 7 || (*data)[6] == 8 || (*data)[6] == 16)) return -1;
        break;
    case CDC_REQUEST_GET_LINE_CODING:
        if (s->bmRequestType != 0xa1 || s->wValue || s->wLength != 7) return -1;
        break;
    case CDC_REQUEST_SET_CONTROL_LINE_STATE:
        if (s->bmRequestType != 0x21 || s->wLength || (s->wValue & ~3u)) return -1;
        break;
    case CDC_REQUEST_SEND_BREAK:
        if (s->bmRequestType != 0x21 || s->wLength) return -1;
        break;
    default: return -1;
    }
    return original_cdc_handler(bus, s, data, len);
}
void usbd_cdc_acm_set_line_coding(uint8_t bus, uint8_t intf, struct cdc_line_coding *coding)
{ line_coding = *coding; }
void usbd_cdc_acm_get_line_coding(uint8_t bus, uint8_t intf, struct cdc_line_coding *coding)
{ *coding = line_coding; }
void usbd_cdc_acm_set_dtr(uint8_t bus, uint8_t intf, bool value) { dtr = value; }
static void check_io(int rc)
{ if (rc) { last_error = rc; ++io_errors; } }
static void read_next(void)
{ if (configured) check_io(usbd_ep_start_read(0, CDC_OUT, echo_buffer, sizeof(echo_buffer))); }
static void out_done(uint8_t bus, uint8_t ep, uint32_t n)
{
    rx_bytes += n;
    /* Backpressure: do not reuse OUT data until IN and its optional ZLP finish. */
    check_io(usbd_ep_start_write(0, CDC_IN, echo_buffer, n));
}
static void in_done(uint8_t bus, uint8_t ep, uint32_t n)
{
    tx_bytes += n;
    if (n && n % 64 == 0) check_io(usbd_ep_start_write(0, CDC_IN, NULL, 0));
    else read_next();
}
static struct usbd_endpoint in_callback = {CDC_IN, in_done}, out_callback = {CDC_OUT, out_done};
static void event(uint8_t bus, uint8_t ev)
{
    switch (ev) {
    case USBD_EVENT_RESET:
    case USBD_EVENT_DISCONNECTED:
    case USBD_EVENT_DEINIT:
        configured = false; dtr = false; break;
    case USBD_EVENT_CONFIGURED:
        configured = true; read_next(); break;
    default: break;
    }
}
static int initialize(void)
{
    usbd_desc_register(0, &descriptors);
    usbd_cdc_acm_init_intf(0, &control_intf);
    original_cdc_handler = control_intf.class_interface_handler;
    control_intf.class_interface_handler = cdc_request;
    memset(&data_intf, 0, sizeof(data_intf));
    usbd_add_interface(0, &control_intf); usbd_add_interface(0, &data_intf);
    usbd_add_endpoint(0, &in_callback); usbd_add_endpoint(0, &out_callback);
    return usbd_initialize(0, 0x200, event);
}
static void poll_once(void)
{
    ab32_usb_poll();
    if (configured && !usb_device_is_configured(0)) {
        configured = false; dtr = false;
        usbd_ep_close(0, CDC_IN); usbd_ep_close(0, CDC_OUT); usbd_ep_close(0, CDC_NOTIFY);
    }
    /* CLEAR_FEATURE(ENDPOINT_HALT) cancels old I/O; resume with a new read. */
    if (configured && ab32_usb_ep_ready(CDC_IN) && ab32_usb_ep_ready(CDC_OUT)) read_next();
}
static void usb_worker(void *arg)
{
    for (;;) {
        uint8_t action = request;
        if (action == 1 && !running) {
            last_error = initialize(); running = last_error == 0;
            rt_kprintf("USB CDC start: %d (native USB port, PB3/PB4)\n", last_error);
        } else if (action == 2 && running) {
            last_error = usbd_deinitialize(0); running = false; configured = false;
        }
        if (action) request = 0;
        if (running) poll_once();
        rt_thread_mdelay(1);
    }
}
static void usbcdc(int argc, char **argv)
{
    if (argc == 2 && !rt_strcmp(argv[1], "status")) {
        rt_kprintf("USB running=%u configured=%u dtr=%u error=%d request=%u\n",
                   running, configured, dtr, last_error, request);
        rt_kprintf("USB reset=%u setup=%u in=%u out=%u errors=%u rx_bytes=%u tx_bytes=%u\n",
                   ab32_usb_stats.resets, ab32_usb_stats.setups, ab32_usb_stats.in_packets,
                   ab32_usb_stats.out_packets, ab32_usb_stats.errors + io_errors, rx_bytes, tx_bytes);
    } else if (argc == 2 && (!rt_strcmp(argv[1], "start") || !rt_strcmp(argv[1], "stop"))) {
        if (request) { rt_kprintf("USB: request pending\n"); return; }
        if (!worker) {
            worker = rt_thread_create("usbcdc", usb_worker, NULL, 2048, 12, 5);
            if (!worker) { rt_kprintf("USB: no memory\n"); return; }
            rt_thread_startup(worker);
        }
        request = !rt_strcmp(argv[1], "start") ? 1 : 2;
    } else rt_kprintf("usbcdc start | status | stop (CDC binary echo; native USB port)\n");
}
MSH_CMD_EXPORT(usbcdc, Native USB CDC ACM echo);
