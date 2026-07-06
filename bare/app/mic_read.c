#include "drv_adc.h"
#include "drv_uart.h"

#define SAMPLE_RATE 16000
#define BUF_SIZE 256

static volatile uint32_t adc_dma_notice_count;
static volatile uint32_t adc_last_event;

static void uart_print_char(char c) {
    drv_uart_putchar(c);
}

static void uart_print_u32(uint32_t value)
{
    char tmp[10];
    int pos = 0;

    if (value == 0) {
        uart_print_char('0');
        return;
    }

    while (value > 0 && pos < (int)sizeof(tmp)) {
        tmp[pos++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (pos > 0) {
        uart_print_char(tmp[--pos]);
    }
}

static void adc_dma_notice(uint32_t event)
{
    adc_last_event = event;
    adc_dma_notice_count++;
}

int main(void)
{
    drv_uart_init(115200);

    drv_adc_cfg_t cfg = {
        .channel = DRV_ADC_CH_MIC,
        .sample_rate = SAMPLE_RATE,
        .gain = 8
    };
    drv_adc_init(&cfg);
    drv_adc_set_dma_samples(BUF_SIZE);
    drv_adc_set_dma_notice_callback(adc_dma_notice);
    drv_adc_start();

    uint32_t last_count = 0;
    while(1)
    {
        if (adc_dma_notice_count != last_count) {
            last_count = adc_dma_notice_count;
            drv_uart_write("sdadc event=", 12);
            uart_print_u32(adc_last_event);
            drv_uart_write(" count=", 7);
            uart_print_u32(last_count);
            drv_uart_write("\r\n", 2);
        }
    }
}
