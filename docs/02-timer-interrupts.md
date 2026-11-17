# Lesson 2 — a timer wakes the CPU

Previously, no input meant immediately checking the UART again. On x86 we now
execute `platform_idle()` when `platform_getc()` returns -1. It uses `hlt` to
stop executing instructions until an enabled interrupt arrives.

The observable result: the prompt still accepts commands, and a timer interrupt
increments `timer_ticks` even when you type nothing. This is a first interrupt
lesson, not yet a real-time scheduler or a fully interrupt-driven UART driver.

## Follow the hardware path

```text
PIT channel 0 produces a periodic signal (about 100 Hz)
  -> legacy PIC receives IRQ0
  -> PIC presents vector 32 to the CPU
  -> CPU uses IDT entry 32 and saves an interrupt return frame
  -> timer_interrupt saves general-purpose registers
  -> timer_tick increments a counter and acknowledges the PIC
  -> wrapper restores registers and executes IRETQ
  -> foreground code resumes, including after a sleeping HLT
```

Read [interrupts.c](../arch/x86_64/interrupts.c). It configures three components:

1. **IDT: Interrupt Descriptor Table.** Each of its 256 entries tells the CPU
   where to enter for a vector. The table occupies 4096 bytes. `lidt` gives its
   address and size to the CPU. Gates select our existing 64-bit GDT code entry.
2. **PIC: Programmable Interrupt Controller.** The legacy PC has two cascaded
   PICs. We remap their vectors above the CPU exception range (0..31) and enable
   only IRQ0. An IRQ is a hardware request line; a vector is the CPU table index
   chosen for it. They are not the same number.
3. **PIT: Programmable Interval Timer.** Its roughly 1.193 MHz input clock is
   divided by 11,931 to produce about 100 ticks per second. This is nominal
   device configuration, not a measured deadline or a precision time source.

We execute `sti`, enabling maskable CPU interrupts, only after these are ready.
Timer interrupts can occur during normal foreground work too, not only at HLT.

## Why an assembly wrapper?

Read [interrupt_entry.S](../arch/x86_64/interrupt_entry.S). A normal C caller knows
it is making a call and follows the calling convention. Interrupted code has
made no such agreement: any register could contain something it still needs.

The wrapper saves all 15 non-stack general-purpose registers, clears the
direction flag for C, and aligns the stack before calling `timer_tick`. It then
restores the registers. All kernel C is compiled with `-mgeneral-regs-only`
and `-mno-red-zone`, so this wrapper need not preserve floating-point/vector
registers or protect a compiler red zone. Those flags are part of the contract.

`iretq` restores the CPU's interrupt return frame, including flags. Ordinary
`ret` cannot do that. The interrupt gate clears the interrupt-enable flag on
entry; restoring flags reestablishes the interrupted code's interrupt state.

The C handler sends an **end-of-interrupt (EOI)** command to the master PIC.
Without acknowledging service completion, the PIC would keep this IRQ marked
in service, preventing repeated timer delivery in this configuration.

Unexpected vectors enter a stop loop. They do not return. This keeps fault
state inspectable, but it is not fault recovery. We have no dedicated interrupt
stack, no normalized exception frames, and no exception messages yet.

For the architecture definition, see Intel's system-programming manuals under
[interrupt and exception handling](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html).

## See a tick in GDB

First exit your old GDB and QEMU sessions. Rebuilding changes symbol addresses;
an old guest and a new ELF do not describe the same running program.

Terminal 1:

```sh
make ARCH=x86_64 check
make ARCH=x86_64 debug
```

Terminal 2, from the project directory:

```sh
gdb -q build/x86_64/gcc/kernel.elf
```

At the GDB prompt:

```gdb
target remote localhost:1234
hbreak *timer_tick
continue
p/d timer_ticks
disassemble timer_tick
```

You are stopped BEFORE the C handler's increment. Reading the counter while
paused does not let time advance in the guest. Execute the handler and inspect
the result:

```gdb
finish
p/d timer_ticks
continue
p/d timer_ticks
```

`finish` stops back in the assembly wrapper after the increment and EOI. The
next `continue` reaches the next timer-handler entry. The counter should have
advanced once. GDB may not unwind the full stack across our assembly interrupt
wrapper; the CPU return frame is not an ordinary function-call frame.

## See the saved interrupted address

Start a fresh debugging session and break at the sleeping foreground function:

```gdb
hbreak *platform_idle
continue
disassemble platform_idle
delete breakpoints
hbreak *timer_interrupt
continue
x/gx $rsp
```

At the exact assembly entry, the top of the CPU-created frame is the saved
instruction pointer. For this idle experiment, it should point at the
instruction after `hlt` in `platform_idle`. The interrupt did not arrive through
a `call` in the foreground code: the timer caused the CPU to transfer control.

After the handler's final `iretq`, execution resumes at that saved address.
Disable breakpoints and continue when you want to use the prompt normally.

## Efficiency and the next experiment

The x86 loop now sleeps between ticks rather than continuously executing UART
checks. We have verified the HLT wakeup and repeated interrupts, not quantified
power savings. Timer ticks are not a measurement of CPU utilization or worst-case
latency, and debugger pauses distort timing experiments.

Serial bytes still wait for the next timer wakeup before being read. This adds
up to roughly one timer period of polling delay in the otherwise idle loop;
there is no hard latency guarantee when other work or debugging intervenes.
Finite UART FIFOs can overflow on real hardware when input arrives faster than
this loop drains it. The next step is a UART interrupt handler and a bounded
receive buffer, with a careful check-and-sleep protocol to avoid lost wakeups.

The timer handler stays short: no console output, allocation, or device waits.
That is a useful habit for bounded embedded interrupt work. A later lesson will
measure execution time and deadline misses rather than relying on averages.

## ARM64 and the Jetson Orin Nano

These legacy PIC/PIT registers are specific to our QEMU PC. ARM64 currently
still polls; its `platform_idle` uses `yield`, not `wfi`, because we have not
configured its timer and interrupt controller. Do not copy PC register values
into the ARM implementation.

The same concepts transfer: vector entry, saved context, device acknowledgment,
and return. The instructions and controllers change. Jetson Orin Nano is our
planned physical target, but QEMU virt's memory map and boot path are not Jetson
board support. NVIDIA's
[Orin boot-flow documentation](https://docs.nvidia.com/jetson/archives/r36.4.4/DeveloperGuide/AR/BootArchitecture/JetsonOrinSeriesBootFlow.html)
describes its firmware stages leading to UEFI. We will investigate that handoff
and board-specific hardware when beginning the Jetson port.
