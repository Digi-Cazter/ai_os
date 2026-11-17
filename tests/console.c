/* Host tests exercise the shared kernel with a fake hardware interface. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <kernel/console.h>
#include <kernel/platform.h>

static char output[8192];
static unsigned position;
static const char *input;
static jmp_buf finished;
static int halted;
static int empty_read;
static unsigned idle_calls;

void platform_init(void) {}
const char *platform_name(void) { return "test platform"; }
void platform_putc(char c)
{
    assert(position + 1 < sizeof(output));
    output[position++] = c;
    output[position] = '\0';
}
int platform_getc(void)
{
    if (empty_read) {
        empty_read = 0;
        return -1; /* Model one check where the device has no input. */
    }
    if (!*input)
        longjmp(finished, 1);
    return (unsigned char)*input++;
}
void platform_idle(void) { ++idle_calls; }
unsigned platform_telemetry_write(const unsigned char *bytes, unsigned length)
{
    (void)bytes;
    (void)length;
    return 0;
}
int platform_time_ms(uint64_t *now_ms)
{
    (void)now_ms;
    return 0; /* The console also works on boards without a clock. */
}
_Noreturn void platform_halt(void)
{
    halted = 1;
    longjmp(finished, 1);
}

static void run(const char *commands)
{
    input = commands;
    position = 0;
    halted = 0;
    empty_read = 1;
    idle_calls = 0;
    output[0] = '\0';
    if (!setjmp(finished))
        kernel_main();
    assert(idle_calls == 1);
}

int main(void)
{
    run("help\r\narch\nversiox\bn\nwrong\nclear\nhalt\n");
    assert(strstr(output, "Commands: help, arch, version, clear, halt\r\n"));
    assert(strstr(output, "aios> \r\naios> ") == NULL); /* CRLF is one enter. */
    assert(strstr(output, "test platform\r\n"));
    assert(strstr(output, "AI OS 0.1.0 - freestanding kernel\r\n"));
    assert(strstr(output, "Unknown command."));
    assert(strstr(output, "\033[2J\033[H"));
    assert(halted);
    char large[400];
    memset(large, 'x', sizeof(large));
    large[sizeof(large) - 2] = '\n';
    large[sizeof(large) - 1] = '\0';
    run(large);
    assert(strstr(output, "Unknown command."));
    run("\b\177\n"); /* Empty backspace and empty command are safe. */
    assert(!strstr(output, "Unknown command."));
    puts("PASS: commands, editing, CRLF, bounded input, idle hook, and halt");
    return 0;
}
