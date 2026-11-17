#include <kernel/platform.h>
#include "io.h"

void interrupts_init(void);

/* The PC UART is accessed through I/O ports, unlike ARM's memory mapped UART. */
enum {
    COM1 = 0x3f8,
    COM2 = 0x2f8,
    UART_DATA = 0,
    UART_INTERRUPT_ENABLE = 1,
    UART_FIFO_CONTROL = 2,
    UART_LINE_CONTROL = 3,
    UART_MODEM_CONTROL = 4,
    UART_LINE_STATUS = 5,
    RX_READY = 1,
    TX_READY = 1 << 5,
};

static void uart_init(unsigned short base)
{
    outb(base + UART_LINE_CONTROL, 0x80); /* DLAB on, expose baud divisor. */
    outb(base + UART_DATA, 1);            /* 115200 baud. */
    outb(base + UART_INTERRUPT_ENABLE, 0);
    outb(base + UART_LINE_CONTROL, 3);    /* DLAB off, 8N1. */
    outb(base + UART_INTERRUPT_ENABLE, 0); /* Disable IRQs in the right bank. */
    outb(base + UART_FIFO_CONTROL, 0xc7); /* Enable/reset the 16-byte FIFOs. */
    outb(base + UART_MODEM_CONTROL, 0x0b);
}

void platform_init(void)
{
    uart_init(COM1); /* Interactive console. */
    uart_init(COM2); /* Framed telemetry, never mixed with console text. */
    interrupts_init();
}

unsigned platform_telemetry_write(const unsigned char *bytes, unsigned length)
{
    if (!(inb(COM2 + UART_LINE_STATUS) & TX_READY))
        return 0;
    /* THRE says FIFO empty. Fill at most its known 16-byte capacity without
     * waiting for individual bytes to finish transmitting on the wire. */
    unsigned count = length < 16 ? length : 16;
    for (unsigned i = 0; i < count; ++i)
        outb(COM2 + UART_DATA, bytes[i]);
    return count;
}

void platform_putc(char c)
{
    while (!(inb(COM1 + UART_LINE_STATUS) & TX_READY))
        __asm__ volatile ("pause");
    outb(COM1 + UART_DATA, (unsigned char)c);
}

int platform_getc(void)
{
    return (inb(COM1 + UART_LINE_STATUS) & RX_READY) ? inb(COM1 + UART_DATA) : -1;
}

void platform_idle(void)
{
    /* IRQ0 wakes us about every 10 ms. UART RX is still polled after wakeup.
     * A tick between the RX check and HLT costs at most one more timer period.
     * Caller is the foreground loop with interrupts enabled, never an ISR. */
    __asm__ volatile ("hlt" : : : "memory");
}

const char *platform_name(void) { return "x86_64 / BIOS PC"; }

_Noreturn void platform_halt(void)
{
    for (;;)
        __asm__ volatile ("cli; hlt");
}
