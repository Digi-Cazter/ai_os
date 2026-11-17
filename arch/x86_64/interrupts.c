#include <stdint.h>
#include <kernel/platform.h>
#include "io.h"

enum {
    PIC_MASTER_COMMAND = 0x20,
    PIC_MASTER_DATA = 0x21,
    PIC_SLAVE_COMMAND = 0xa0,
    PIC_SLAVE_DATA = 0xa1,
    TIMER_VECTOR = 32,
    PIT_CHANNEL0 = 0x40,
    PIT_COMMAND = 0x43,
    PIT_INPUT_HZ = 1193182,
    TIMER_HZ = 100,
    KERNEL_CODE_SELECTOR = 0x18, /* The 64-bit code entry in start.S's GDT. */
    INTERRUPT_GATE = 0x8e,      /* Present, privilege level 0, interrupt gate. */
};

/* Hardware-defined 16-byte layout: the handler address is split in thirds. */
struct idt_gate {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct idt_pointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

_Static_assert(sizeof(struct idt_gate) == 16, "CPU expects 16-byte IDT gates");
_Static_assert(sizeof(struct idt_pointer) == 10, "CPU expects a 10-byte IDTR operand");

static struct idt_gate idt[256] __attribute__((aligned(16)));
/* Single CPU, one writer (IRQ0); volatile keeps GDB-visible updates in RAM.
 * This is NOT a general-purpose synchronization scheme for multiple CPUs. */
volatile uint64_t timer_ticks;

int platform_time_ms(uint64_t *now_ms)
{
    /* One aligned 64-bit read on our single x86_64 CPU. Only the ISR writes
     * this counter. Resolution is 10 ms; PIT frequency is approximate. */
    *now_ms = timer_ticks * (1000 / TIMER_HZ);
    return 1;
}

extern void timer_interrupt(void);
extern void unexpected_interrupt(void);

static void set_gate(unsigned vector, void (*entry)(void))
{
    uint64_t address = (uint64_t)entry;
    idt[vector] = (struct idt_gate) {
        .offset_low = (uint16_t)address,
        .selector = KERNEL_CODE_SELECTOR,
        .ist = 0, /* Use the current kernel stack; no separate interrupt stack. */
        .attributes = INTERRUPT_GATE,
        .offset_middle = (uint16_t)(address >> 16),
        .offset_high = (uint32_t)(address >> 32),
        .reserved = 0,
    };
}

static void pic_write(unsigned short port, unsigned char value)
{
    outb(port, value);
    outb(0x80, 0); /* Traditional PC I/O delay between controller commands. */
}

void interrupts_init(void)
{
    __asm__ volatile ("cli" : : : "memory");
    for (unsigned i = 0; i < 256; ++i)
        set_gate(i, unexpected_interrupt);
    set_gate(TIMER_VECTOR, timer_interrupt);
    const struct idt_pointer pointer = {
        .limit = sizeof(idt) - 1,
        .base = (uint64_t)idt,
    };
    __asm__ volatile ("lidt %0" : : "m"(pointer) : "memory");

    /* The BIOS IRQ vector assignments overlap CPU exceptions. Move the two
     * legacy PICs to 32..39 and 40..47 before enabling external interrupts. */
    pic_write(PIC_MASTER_DATA, 0xff);
    pic_write(PIC_SLAVE_DATA, 0xff);
    pic_write(PIC_MASTER_COMMAND, 0x11); /* Initialize, cascaded, ICW4 follows. */
    pic_write(PIC_SLAVE_COMMAND, 0x11);
    pic_write(PIC_MASTER_DATA, TIMER_VECTOR);
    pic_write(PIC_SLAVE_DATA, 40);
    pic_write(PIC_MASTER_DATA, 1 << 2); /* Slave is connected to master IRQ2. */
    pic_write(PIC_SLAVE_DATA, 2);       /* Slave identity on that connection. */
    pic_write(PIC_MASTER_DATA, 0x01);   /* 8086 mode, explicit end-of-interrupt. */
    pic_write(PIC_SLAVE_DATA, 0x01);
    pic_write(PIC_MASTER_DATA, 0xfe);   /* Only IRQ0 (timer) unmasked. */
    pic_write(PIC_SLAVE_DATA, 0xff);    /* No slave interrupts for this lesson. */

    /* PIT channel 0: low byte then high byte, mode 2 (rate generator), binary.
     * 1,193,182 / 11,931 is about 100 Hz, one tick approximately every 10 ms. */
    const uint16_t divisor = PIT_INPUT_HZ / TIMER_HZ;
    outb(PIT_COMMAND, 0x34);
    outb(PIT_CHANNEL0, (unsigned char)divisor);
    outb(PIT_CHANNEL0, (unsigned char)(divisor >> 8));

    /* Enable only AFTER the table, controller, and device are ready. */
    __asm__ volatile ("sti" : : : "memory");
}

/* Called by our assembly wrapper. Keep interrupt work short: no printing,
 * allocation, or waiting for devices. Interrupt gates disable nested IRQs. */
void timer_tick(void)
{
    ++timer_ticks;
    outb(PIC_MASTER_COMMAND, 0x20); /* EOI: allow the PIC to deliver another IRQ. */
}
