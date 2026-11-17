#include <kernel/platform.h>

/* QEMU virt board PL011. Real boards need their own mapping and clocks. */
static volatile unsigned int *const uart = (volatile unsigned int *)0x09000000UL;
/* Register offsets are in bytes; uart[] indexes 32-bit (4-byte) registers. */
enum {
    DATA = 0x00 / 4,
    FLAGS = 0x18 / 4,
    INTEGER_BAUD = 0x24 / 4,
    FRACTIONAL_BAUD = 0x28 / 4,
    LINE_CONTROL = 0x2c / 4,
    CONTROL = 0x30 / 4,
    INTERRUPT_MASK = 0x38 / 4,
    INTERRUPT_CLEAR = 0x44 / 4,
    RX_EMPTY = 1 << 4,
    TX_FULL = 1 << 5,
};

void platform_init(void)
{
    uart[CONTROL] = 0;
    uart[INTERRUPT_CLEAR] = 0x7ff;
    uart[INTERRUPT_MASK] = 0; /* Polling for now; no interrupt controller yet. */
    /* 24 MHz / (16 * 115200) = 13 + 1/64 (rounded). */
    uart[INTEGER_BAUD] = 13;
    uart[FRACTIONAL_BAUD] = 1;
    uart[LINE_CONTROL] = (3 << 5) | (1 << 4); /* 8 data bits, FIFO enabled. */
    uart[CONTROL] = (1 << 9) | (1 << 8) | 1; /* RX, TX, UART enabled. */
}

void platform_putc(char c)
{
    while (uart[FLAGS] & TX_FULL)
        __asm__ volatile ("yield");
    uart[DATA] = (unsigned char)c;
}

int platform_getc(void)
{
    return (uart[FLAGS] & RX_EMPTY) ? -1 : (int)(uart[DATA] & 0xff);
}

void platform_idle(void)
{
    /* ARM timer/GIC support is a separate lesson. WFI here would have no
     * configured wakeup source, so ARM64 intentionally still polls. */
    __asm__ volatile ("yield");
}

int platform_time_ms(uint64_t *now_ms)
{
    (void)now_ms;
    return 0; /* No ARM timer yet. Do not invent time from polling speed. */
}

unsigned platform_telemetry_write(const unsigned char *bytes, unsigned length)
{
    (void)bytes;
    (void)length;
    return 0; /* No second ARM UART/telemetry transport configured yet. */
}

const char *platform_name(void) { return "arm64 / QEMU virt"; }

_Noreturn void platform_halt(void)
{
    __asm__ volatile ("msr daifset, #0xf");
    for (;;)
        __asm__ volatile ("wfi");
}
