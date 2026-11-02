/* Exercise the actual FinSH loop. Emit a byte trace for an ANSI terminal model. */
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rtthread.h>
#include <rtdevice.h>
#include "../../../rtt/components/finsh/shell.c"

static jmp_buf finished;
static unsigned char input[512];
static size_t input_len, input_pos;
static struct rt_device device;
static struct finsh_shell state;
static void trace(char kind, const void *bytes, size_t n)
{
    const unsigned char *p = bytes;
    printf("%c ", kind);
    for (size_t i = 0; i < n; i++) printf("%02x", p[i]);
    putchar('\n');
}
int rt_kprintf(const char *format, ...)
{
    char buffer[1024], stream[2048];
    va_list ap; va_start(ap, format);
    int n = vsnprintf(buffer, sizeof(buffer), format, ap); va_end(ap);
    assert(n >= 0 && n < (int)sizeof(buffer));
    size_t used = 0;
    for (int i = 0; i < n; i++) {
        if (buffer[i] == '\n') stream[used++] = '\r'; /* RT serial STREAM */
        stream[used++] = buffer[i];
    }
    trace('O', stream, used);
    return n;
}
rt_ssize_t rt_device_read(rt_device_t d, rt_off_t pos, void *buffer, rt_size_t n)
{
    assert(d == &device && pos == -1 && n == 1);
    if (input_pos == input_len) {
        trace('L', shell->line, shell->line_position);
        printf("P %u\n", shell->line_curpos);
        longjmp(finished, 1);
    }
    trace('I', &input[input_pos], 1);
    *(unsigned char *)buffer = input[input_pos++];
    return 1;
}
int msh_exec(char *line, rt_size_t length)
{ trace('E', line, length); if (length) rt_kprintf("RESULT\n"); return 0; }
void msh_auto_complete(char *line)
{ if (!strcmp(line, "ver")) strcpy(line, "version"); }
void msh_opt_auto_complete(char *line) { (void)line; }
rt_size_t rt_strlen(const char *s) { return strlen(s); }
rt_int32_t rt_strcmp(const char *a, const char *b) { return strcmp(a, b); }
void *rt_memset(void *p, int c, rt_ubase_t n) { return memset(p, c, n); }
void *rt_memcpy(void *d, const void *s, rt_ubase_t n) { return memcpy(d, s, n); }
void *rt_memmove(void *d, const void *s, rt_ubase_t n) { return memmove(d, s, n); }
rt_device_t rt_console_get_device(void) { return &device; }
rt_device_t rt_device_find(const char *s) { (void)s; return &device; }
rt_err_t rt_device_open(rt_device_t d, rt_uint16_t f) { (void)d; (void)f; return 0; }
rt_err_t rt_device_close(rt_device_t d) { (void)d; return 0; }
rt_err_t rt_device_set_rx_indicate(rt_device_t d, rt_err_t (*f)(rt_device_t, rt_size_t))
{ (void)d; (void)f; return 0; }
rt_err_t rt_sem_take(rt_sem_t s, rt_int32_t t) { (void)s; (void)t; abort(); }
rt_err_t rt_sem_release(rt_sem_t s) { (void)s; return 0; }
void rt_assert_handler(const char *e, const char *f, rt_size_t line)
{ fprintf(stderr, "assert %s %s:%u\n", e, f, (unsigned)line); abort(); }
int main(int argc, char **argv)
{
    assert(argc == 3 && strlen(argv[1]) / 2 < sizeof(input));
    for (size_t i = 0; argv[1][i]; i += 2) {
        unsigned v; assert(sscanf(&argv[1][i], "%2x", &v) == 1); input[input_len++] = v;
    }
    strcpy(device.parent.name, argv[2]);
    shell = &state; shell->device = &device; shell->prompt_mode = 1;
    if (!setjmp(finished)) finsh_thread_entry(NULL);
    return 0;
}
