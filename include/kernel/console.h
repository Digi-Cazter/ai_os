#ifndef KERNEL_CONSOLE_H
#define KERNEL_CONSOLE_H

void console_write(const char *text);
void shell_execute(const char *line);
_Noreturn void kernel_main(void);

#endif
