#ifndef HEARTBEAT_BOARD_H
#define HEARTBEAT_BOARD_H
#include <stdint.h>
void board_init(void);
void board_delay_ms(uint32_t ms);
void board_puts(const char *text);
void board_watchdog_feed(void);
uint32_t board_millis(void);
int board_uart_getc(void);
void board_reset(void);
#endif
