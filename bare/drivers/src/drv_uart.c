#include "drv_uart.h"

/* libhal.a 符号 */
extern void huart_init_do(uint32_t baud);
extern void huart_putchar(char c);
extern char huart_getchar(void);
extern void huart_rxfifo_clear(void);
extern void huart_tx_done(void);

bool drv_uart_init(uint32_t baudrate)
{
    huart_init_do(baudrate);
    huart_rxfifo_clear();
    return true;
}

void drv_uart_putchar(char c)
{
    huart_putchar(c);
}

char drv_uart_getchar(void)
{
    return huart_getchar();
}

void drv_uart_write(const char *buf, uint32_t len)
{
    if(!buf || len == 0) return;

    for(uint32_t i=0; i<len; i++)
        huart_putchar(buf[i]);

    /* 等待 TX 完成（可选） */
    huart_tx_done();
}

uint32_t drv_uart_read(char *buf, uint32_t len)
{
    if(!buf || len == 0) return 0;

    uint32_t cnt = 0;
    for(uint32_t i=0; i<len; i++)
    {
        buf[i] = huart_getchar();
        cnt++;
    }
    return cnt;
}