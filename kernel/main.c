#include <kernel/console.h>
#include <kernel/platform.h>
#include <kernel/sensor.h>
#include <kernel/controller.h>
#include <kernel/telemetry.h>

void console_write(const char *text)
{
    while (*text) {
        if (*text == '\n')
            platform_putc('\r');
        platform_putc(*text++);
    }
}

static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

void shell_execute(const char *line)
{
    if (equal(line, "help"))
        console_write("Commands: help, arch, version, clear, halt\n");
    else if (equal(line, "arch")) {
        console_write(platform_name());
        console_write("\n");
    } else if (equal(line, "version"))
        console_write("AI OS 0.1.0 - freestanding kernel\n");
    else if (equal(line, "clear"))
        console_write("\033[2J\033[H");
    else if (equal(line, "halt")) {
        console_write("CPU halted. Exit QEMU with Ctrl-a x.\n");
        platform_halt();
    } else if (*line)
        console_write("Unknown command. Type help.\n");
}

_Noreturn void kernel_main(void)
{
    char line[128];
    unsigned length = 0;
    int previous_cr = 0;
    platform_init();
    console_write("\nAI OS 0.1.0\nPlatform: ");
    console_write(platform_name());
    console_write("\nKernel ready. Type help.\naios> ");
    uint64_t now_ms;
    int clock_available = platform_time_ms(&now_ms);
    if (clock_available) {
        sensor_init(now_ms);
        controller_init(now_ms);
        telemetry_init(now_ms);
    }
    for (;;) {
        /* Timer ISR only counts ticks. Dispatch foreground work even when
         * serial input is continuous, not just in the no-input branch. */
        if (clock_available && platform_time_ms(&now_ms)) {
            periodic_dispatch(&sensor_task, now_ms);
            /* Refresh start time after the sensor callback. Fixed order means
             * the controller can consume a sample published in this iteration. */
            if (platform_time_ms(&now_ms))
                periodic_dispatch(&controller_task, now_ms);
            if (platform_time_ms(&now_ms))
                periodic_dispatch(&telemetry_task, now_ms);
        }
        telemetry_drain();
        int c = platform_getc();
        if (c < 0) {
            platform_idle();
            continue;
        }
        if (c == '\n' && previous_cr) {
            previous_cr = 0;
            continue;
        }
        previous_cr = c == '\r';
        if (c == '\r' || c == '\n') {
            console_write("\n");
            line[length] = '\0';
            shell_execute(line);
            length = 0;
            console_write("aios> ");
        } else if (c == '\b' || c == 127) {
            if (length) {
                --length;
                console_write("\b \b");
            }
        } else if (c >= 32 && c <= 126 && length < sizeof(line) - 1) {
            line[length++] = (char)c;
            platform_putc((char)c);
        }
    }
}
