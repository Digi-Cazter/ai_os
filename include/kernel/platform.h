#ifndef KERNEL_PLATFORM_H
#define KERNEL_PLATFORM_H

#include <stdint.h>

/* Each architecture/board supplies this interface. No host OS or libc calls. */
void platform_init(void);
void platform_putc(char c);
int platform_getc(void);
/* Called by the foreground loop when no input is available. May wait for an
 * interrupt on boards with interrupt support; must eventually return. */
void platform_idle(void);
/* Return 1 and set *now_ms if a monotonic clock is implemented; return 0 on
 * unsupported boards. A zero timestamp is valid and does not mean failure. */
int platform_time_ms(uint64_t *now_ms);
/* Nonblocking telemetry transport. Return bytes accepted, in [0, length].
 * A return value of zero is normal backpressure, not permission to wait. */
unsigned platform_telemetry_write(const unsigned char *bytes, unsigned length);
const char *platform_name(void);
_Noreturn void platform_halt(void);

#endif
