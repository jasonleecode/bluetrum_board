"""Exercise the real firmware parser with a simulated board clock and LED."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which("cc"), "host C compiler required")
class FirmwareConsoleTests(unittest.TestCase):
    def test_commands_overflow_and_heartbeat_wrap(self):
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "serial_console.h"
static uint32_t now;
static bool led;
static char output[512];
static unsigned resets;
void drv_led_init(void) {}
void drv_led_set(bool on) { led = on; }
uint32_t board_millis(void) { return now; }
void board_reset(void) { ++resets; }
void board_puts(const char *s) { strcat(output, s); }
static void send(const char *s) {
    output[0] = 0;
    while (*s) serial_console_rx((uint8_t)*s++);
}
int main(void) {
    serial_console_init();
    assert(led);
    now = 499; serial_console_poll(); assert(led);
    now = 500; serial_console_poll(); assert(!led);
    send("ping\r\n"); assert(!strcmp(output, "OK pong\r\n"));
    send("echo abc 123\n"); assert(!strcmp(output, "OK abc 123\r\n"));
    send("echo\n"); assert(!strcmp(output, "OK \r\n"));
    send("led off\n"); assert(!led);
    now += 2000; serial_console_poll(); assert(!led);
    send("status\n"); assert(strstr(output, "mode=manual led=off uptime_ms=2500"));
    send("led on\r"); assert(led);
    send("led ofx\bf\n"); assert(!led);
    send("led blink\n"); assert(led);
    now += 500; serial_console_poll(); assert(!led);
    send("led on\x01\n"); assert(strstr(output, "ERR printable")); assert(!led);
    send("rebootXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
         "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n");
    assert(strstr(output, "ERR line too long")); assert(resets == 0);
    send("ping\n"); assert(!strcmp(output, "OK pong\r\n"));
    send("unknown\n"); assert(strstr(output, "ERR unknown"));
    send("reboot\n"); assert(resets == 1);
    now = UINT32_MAX - 200;
    serial_console_init();
    now += 499; serial_console_poll(); assert(led);
    now += 1; serial_console_poll(); assert(!led);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "check.c"
            target = Path(directory) / "check"
            source.write_text(harness)
            subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "bare/drivers/inc"), str(source),
                            str(ROOT / "bare/drivers/src/serial_console.c"),
                            "-o", str(target)], check=True, capture_output=True)
            subprocess.run([str(target)], check=True, capture_output=True, timeout=5)


if __name__ == "__main__":
    unittest.main()
