# Lesson 1 — who calls our kernel?

The CPU does not understand C files, ELF files, or an operating system.
Something must put machine instructions into memory and direct the CPU to
their first address. Firmware begins that process. Our boot code then creates
the environment that a compiled C function expects.

For this lesson, the observable result is a serial message and a working
`arch` command. The prompt is a kernel function, not a separate userspace program.

## 1. Follow the x86 path

```text
QEMU emulates a PC
  -> its BIOS loads our first disk sector at physical address 0x7c00
  -> boot.S asks BIOS to load the next 64 sectors at 0x10000
  -> start.S switches CPU modes and establishes the C environment
  -> kernel_main() initializes the UART and prints a message
```

Open [boot.S](../arch/x86_64/boot.S) first. GNU assembly here uses AT&T syntax:
`$` means an immediate number, `%` introduces a register, and the destination
comes last. `movw $0x7c00, %sp` puts the number `0x7c00` into the 16-bit stack
pointer. Suffixes `b`, `w`, `l`, and `q` mean 8, 16, 32, and 64 bits.

The BIOS starts this path in **16-bit real mode**. The initial far jump makes
the code segment predictable. We establish data segments and a stack because
firmware's register values are not our C runtime. In real mode, a physical
address is normally `segment * 16 + offset`: the destination `0x1000:0`
therefore means physical address `0x10000`.

The boot disk number arrives in register `DL`. We save it and call BIOS disk
services with `int $0x13`. Function `0x41` checks extended disk support;
function `0x42` reads sectors described by our disk address packet. The
packet requests 64 sectors starting at logical block address 1. Each sector
is 512 bytes, so our current on-disk kernel limit is 32 KiB. The image script
refuses a larger kernel rather than silently truncating it.

The last two bytes of sector 0 are `55 aa`. They identify a bootable sector
to this BIOS path. They are a marker, not executable instructions or a security
signature. This loader expects BIOS extensions and an IDE-style hard disk;
it is not a floppy loader or a UEFI executable.

**Experiment:** build with `make check`, then run:

```sh
od -An -tx1 -j510 -N2 build/x86_64/gcc/aios.img
```

Predict the two bytes before executing the command. Then find the assembly
directive that placed them there. Why do we skip disk sector 0 when reading
the kernel?

## 2. Create the x86_64 execution environment

Now read [start.S](../arch/x86_64/start.S). Its stages are:

1. Enable A20, a legacy address-line gate needed for reliable access above
   1 MiB on this PC path. Load a **GDT**, the descriptor table defining our
   code and data segments.
2. Set the protection-enable bit in **CR0**, a CPU control register. A far
   jump loads our 32-bit code selector: we are now in protected mode.
3. Check for long-mode support using `CPUID`. Clear **BSS**, the memory region
   for zero-initialized global/static objects, including our page tables.
4. Build three levels of page tables. Each page-directory entry maps a 2 MiB
   page; 512 entries identity-map the first 1 GiB. Identity mapping means a
   virtual address equals its physical address.
5. Enable PAE in **CR4**, put the root page-table address in **CR3**, enable
   long mode in the **EFER** model-specific register, and enable paging in CR0.
6. Far-jump into our 64-bit code segment. Set an aligned stack and call C.

The low bits `3` in page-table pointers mean present and writable. Leaf entries
use `0x83`: those two bits plus bit 7, selecting large pages. The upper halves
of the entries are zero because we cleared the table memory first. These broad
bootstrap mappings are temporary; there is no memory isolation yet.

The GDT's four entries are null, 32-bit code, data, and 64-bit code. A selector
is an index times 8, giving `0x08`, `0x10`, and `0x18`. Those values are offsets
inside the GDT, not physical memory addresses.

The linker script starts the kernel at `0x10000`, matching the disk loader's
destination. Moving only one of those addresses breaks the agreement between
the loader and kernel. The stack grows downward from `stack_top`. We reserve
4 KiB; its top is aligned to 16 bytes before the C `call`, as required by our
x86_64 calling convention.

Interrupts remain disabled throughout this assembly startup. In Lesson 2,
`platform_init()` installs an interrupt descriptor table and enables a timer.
Unexpected exceptions then stop in `unexpected_interrupt` for GDB inspection;
full diagnostic fault reporting is still a separate milestone.

**Experiment:** locate `_start`, `long_mode`, and `kernel_main` in
`build/x86_64/gcc/kernel.map`. Which is the ELF entry point? Why can't BIOS jump
straight into `kernel_main`?

## 3. Compare the ARM64 handoff

ARM64 is the architecture; **QEMU virt** is our current board. Its boot path is:

```text
QEMU's direct ELF loader places segments at their linked addresses
  -> CPU starts at our ELF entry, 0x40080000
  -> ARM start.S sets the stack and clears BSS
  -> the same kernel_main() runs
```

This is a deliberately simpler handoff. It does not yet exercise UEFI,
Trusted Firmware-A, or a real board's boot ROM. Our command uses one CPU,
`secure=off`, and `virtualization=off` so the guest starts in nonsecure **EL1**,
the ARM exception level intended for an OS kernel. We do not yet handle an
EL2/EL3 entry or secondary CPU startup. The MMU remains off.

Read [ARM start.S](../arch/arm64/start.S). ARM syntax puts the destination
first, so `mov sp, x0` copies register `x0` to the stack pointer. `adrp` plus
`add` constructs a symbol address. `str xzr, [x0], #8` stores eight zero bytes
and advances the pointer by eight. Unlike x86 `call`, ARM's `bl` puts the return
address in a register; the C compiler saves it when needed.

The board's RAM begins at `0x40000000`; we link at `0x40080000`. Its PL011 UART
is at `0x09000000`. These are **board choices**, not properties of the ARM64
instruction set. Later we will discover device addresses from a device tree.
QEMU documents its [ARM virt boot behavior](https://www.qemu.org/docs/master/system/arm/virt.html).

## 4. See what stays portable

Read [platform.h](../include/kernel/platform.h), then the two `platform.c` files.
The shared kernel asks for a character to be written; the board implementation
decides how to do that.

On the PC, `inb`/`outb` access a separate I/O-port address space. On ARM virt,
ordinary loads/stores access **memory-mapped I/O** registers. `volatile` tells
the compiler these accesses must actually happen rather than being cached in
a register or optimized away. It is not a general substitute for CPU memory
barriers; we will revisit ordering when enabling caches and multiple CPUs.

Both drivers **poll** a status register: repeatedly inspect whether the device
can send or receive. This keeps the first lesson independent of interrupt
controllers. `platform_getc()` returns `-1` if no byte is ready.

Read [main.c](../kernel/main.c) last. All kernel console output passes through
the platform interface, so the same source builds for both instruction sets.
The build uses `-ffreestanding` because no host OS or standard C runtime exists
underneath us. The startup assembly supplies the stack and zeroed BSS; the
linker script supplies the memory layout. There is no `printf` hiding a Linux
system call.

**Experiment:** change the welcome message in `kernel_main`, rebuild, and
observe it in each emulator. Then add a `lesson` command that prints one fact
you learned. It should require no architecture-specific changes.

## What firmware integration means next

Our BIOS calls are firmware integration already, but they stop before entering
protected/long mode. We cannot keep calling real-mode BIOS services as if they
were normal 64-bit C functions. Drivers eventually replace those services.

The next useful firmware lesson is **discovering usable RAM**: call BIOS E820
while still in real mode, preserve its result across the handoff, and teach
the portable kernel to consume a common boot-information structure. Compare
that with ARM's device tree and later UEFI's memory map. That gives us a reason
to introduce each abstraction instead of building it speculatively.

Before moving on, explain in your own words: who loaded our bytes, what CPU
state C needs, and which assumptions belong to a CPU architecture versus a
particular board.
