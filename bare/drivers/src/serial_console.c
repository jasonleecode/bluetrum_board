#include "serial_console.h"
#include "heartbeat_board.h"
#include "drv_led.h"
#include <stdbool.h>

#define LINE_SIZE 96u
static char line[LINE_SIZE];
static unsigned length;
static bool overflow, invalid, led_on, blinking;
static uint32_t next_toggle;

static bool equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static bool prefix(const char *a, const char *b)
{
    while (*b) {
        if (*a++ != *b++) return false;
    }
    return true;
}

static char *append(char *out, const char *text)
{
    while (*text) *out++ = *text++;
    *out = 0;
    return out;
}

static char *decimal(char *out, uint32_t value)
{
    char digits[10];
    unsigned n = 0;
    do { digits[n++] = '0' + value % 10; value /= 10; } while (value);
    while (n) *out++ = digits[--n];
    *out = 0;
    return out;
}

void serial_console_init(void)
{
    length = 0;
    overflow = invalid = false;
    blinking = led_on = true;
    drv_led_init();
    drv_led_set(led_on);
    next_toggle = board_millis() + 500;
}

static void execute(void)
{
    char reply[160];
    if (equal(line, "ping")) {
        board_puts("OK pong\r\n");
    } else if (equal(line, "help")) {
        board_puts("OK ping | status | echo TEXT | led on | led off | led blink | reboot\r\n");
    } else if (equal(line, "status")) {
        char *p = append(reply, "OK mode=");
        p = append(p, blinking ? "blink" : "manual");
        p = append(p, led_on ? " led=on uptime_ms=" : " led=off uptime_ms=");
        p = decimal(p, board_millis());
        append(p, "\r\n");
        board_puts(reply);
    } else if (equal(line, "echo") || prefix(line, "echo ")) {
        char *p = append(reply, "OK ");
        p = append(p, line[4] ? line + 5 : "");
        append(p, "\r\n");
        board_puts(reply);
    } else if (equal(line, "led on") || equal(line, "led off") || equal(line, "led blink")) {
        blinking = equal(line, "led blink");
        led_on = !equal(line, "led off");
        drv_led_set(led_on);
        next_toggle = board_millis() + 500;
        char *p = append(reply, "OK ");
        p = append(p, line);
        append(p, "\r\n");
        board_puts(reply);
    } else if (equal(line, "reboot")) {
        board_puts("OK reboot\r\n");
        board_reset();
    } else {
        board_puts("ERR unknown command; use help\r\n");
    }
}

void serial_console_rx(uint8_t value)
{
    if (value == '\r' || value == '\n') {
        if (overflow) board_puts("ERR line too long (max 95 bytes)\r\n");
        else if (invalid) board_puts("ERR printable ASCII required\r\n");
        else if (length) { line[length] = 0; execute(); }
        length = 0;
        overflow = invalid = false;
    } else if (value == '\b' || value == 127) {
        if (!overflow && length) --length;
    } else if (value < 32 || value > 126) {
        invalid = true;
    } else if (length < LINE_SIZE - 1) {
        line[length++] = value;
    } else {
        overflow = true; /* Discard until newline; never execute a truncated command. */
    }
}

void serial_console_poll(void)
{
    uint32_t now = board_millis();
    if (blinking && (int32_t)(now - next_toggle) >= 0) {
        led_on = !led_on;
        drv_led_set(led_on);
        next_toggle = now + 500;
    }
}
