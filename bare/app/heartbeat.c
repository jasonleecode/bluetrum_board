#include "heartbeat_board.h"
#include "serial_console.h"

int main(void)
{
    serial_console_init();
    /* Speak only in response to the host: unsolicited logs collide with input
     * on PA7, which is shared by TX and RX on the programming connector.
     */
    for (;;) {
        board_watchdog_feed();
        int value = board_uart_getc();
        if (value >= 0) serial_console_rx((uint8_t)value);
        serial_console_poll();
    }
}
