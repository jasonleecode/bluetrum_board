#ifndef SERIAL_CONSOLE_H
#define SERIAL_CONSOLE_H
#include <stdint.h>
void serial_console_init(void);
void serial_console_rx(uint8_t value);
void serial_console_poll(void);
#endif
