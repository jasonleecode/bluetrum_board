#pragma once
#include <stdint.h>
#include <stdbool.h>

/* 初始化 UART */
bool drv_uart_init(uint32_t baudrate);

/* 发送一个字节 */
void drv_uart_putchar(char c);

/* 接收一个字节 */
char drv_uart_getchar(void);

/* 发送缓冲区数据 */
void drv_uart_write(const char *buf, uint32_t len);

/* 接收缓冲区数据 */
uint32_t drv_uart_read(char *buf, uint32_t len);