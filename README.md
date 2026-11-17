# AI OS — a small operating systems learning project

We are building a freestanding kernel in C and assembly for **x86_64 and
ARM64 (AArch64)**. The first milestone is understanding how execution reaches
our C code. This is a kernel skeleton with a serial command prompt, not yet a
general-purpose OS.

Efficiency and a small footprint are design goals. We compile with `-Os`,
discard unused code/data sections, reserve a 4 KiB bootstrap stack, and use
fixed-size console storage rather than dynamic allocation. `make size` reports
initialized kernel bytes, reserved RAM, and artifact sizes separately. Debug
symbols stay in the ELF for learning; they are not loaded into guest RAM.
The x86 disk image is just 65 sectors (33,280 bytes), including its fixed
32 KiB kernel loading region. Later features must justify their memory and
code-size costs. We favor understandable code over clever size tricks.

Start with [Lesson 1: firmware to C](docs/01-firmware-to-c.md). Read one stage,
inspect its code, and try its exercise before moving to the next stage.
Then follow [Lesson 2: timer interrupts and sleeping](docs/02-timer-interrupts.md).
Next is [Lesson 3: a periodic synthetic sensor](docs/03-periodic-sensor.md).
Then [Lesson 4: bounded messages](docs/04-bounded-messages.md) connects it to a
controller task and explores queue overflow and stale data.
[Lesson 5: live telemetry](docs/05-live-telemetry.md) observes those values on
the host, with recording and replay.

Our direction is a **small robotics kernel**: bounded message passing, periodic
tasks, and telemetry that the same host-side visualization tools can read from
simulation or physical hardware. It is ROS-inspired, not a ROS 2 implementation.
The first vehicle is a four-wheel rover with steerable front wheels. Read the
[runtime and visualization design](docs/robotics-runtime.md) for boundaries and
the incremental implementation sequence. A cooperative 10 Hz synthetic sensor
publishes to a four-slot queue, and a 20 Hz controller stores a speed decision
on x86. A separate COM2 telemetry stream feeds a live terminal viewer with
recording/replay. Rover physics and physical actuation are still planned.

Our first planned physical board is the **Jetson Orin Nano**. It is not yet a
supported boot target. ARM64 currently means QEMU virt; a Jetson port will need
its own firmware handoff and board drivers. NVIDIA documents the
[Orin firmware-to-UEFI boot flow](https://docs.nvidia.com/jetson/archives/r36.4.4/DeveloperGuide/AR/BootArchitecture/JetsonOrinSeriesBootFlow.html).

## Build and run

On an Ubuntu/Debian development machine, these packages provide the GNU tools
and emulators (install them yourself if they are missing):

```sh
sudo apt install build-essential binutils python3 gcc-aarch64-linux-gnu \
  binutils-aarch64-linux-gnu qemu-system-x86 qemu-system-arm
```

```sh
make ARCH=x86_64 check
make ARCH=x86_64 run

make ARCH=arm64 check
make ARCH=arm64 run
make ARCH=x86_64 size
```

For live observation, first exit other QEMU/GDB sessions. In terminal 1 run
`make ARCH=x86_64 observe`. In terminal 2, from this project directory:

```sh
python3 scripts/telemetry_view.py --input build/x86_64/gcc/telemetry.bin --follow
```

The console stays in terminal 1; terminal 2 shows received distance history,
queue occupancy, controller decisions, and loss counters. See Lesson 5 for
recording/replay, GDB alongside observation (`make observe-debug`), protocol
details, and the future physical-serial transport.

The console appears in your terminal. Try `help`, `arch`, `version`, `clear`,
and `halt`. Exit QEMU with **Ctrl-a, then x**. `halt` stops the guest CPU; it
does not power off the virtual machine.

On x86, the prompt sleeps with `hlt` when no character is available. A legacy
PIT timer wakes it approximately 100 times per second to poll the UART. Serial
input is not yet interrupt-driven; receive latency can approach a timer period,
and physical UART input bursts can overrun its FIFO. ARM64 still polls because
its timer and interrupt controller have not been implemented. Small binaries,
low CPU usage, and timely input are separate metrics.

Build outputs go into `build/<architecture>/<toolchain>/`. The x86 target
produces a raw BIOS disk image, `aios.img`; ARM64 boots `kernel.elf` directly.
Architecture names describe instruction sets. Our supported boards are a
BIOS PC and QEMU's ARM `virt`, not every machine using those instruction sets.

### Cross-compilation

The compiler runs on your development machine and emits instructions for the
selected target. Nothing needs to run on an ARM board to build the ARM kernel.
The shared `kernel/main.c` is compiled separately for each architecture.

GNU defaults are `x86_64-linux-gnu-gcc`/`ld` and
`aarch64-linux-gnu-gcc`/`ld`. Although these compilers normally target Linux,
our freestanding flags and direct linker invocation use no Linux runtime,
libc, startup files, or system calls. Dedicated bare-metal toolchains work too:

```sh
make ARCH=x86_64 KCC=x86_64-elf-gcc KLD=x86_64-elf-ld
```

Alternatively, install `clang` and `lld` and use their built-in cross targets:

```sh
make ARCH=x86_64 TOOLCHAIN=llvm check
make ARCH=arm64 TOOLCHAIN=llvm check
```

See the [Clang cross-compilation documentation](https://clang.llvm.org/docs/CrossCompilation.html)
for how target triples select a CPU architecture and environment. Compiling
software *inside* our OS is a separate, much later project.

## Find your way around

| File or directory | Responsibility |
| --- | --- |
| `arch/x86_64/boot.S` | Our 512-byte BIOS disk loader |
| `arch/*/start.S` | Establish CPU state, stack, and zero-initialized memory |
| `arch/*/linker.ld` | Decide where code and data live in memory |
| `arch/*/platform.c` | Board-specific serial I/O and CPU halt |
| `arch/x86_64/interrupts.c` | IDT, legacy PIC, PIT timer, and tick counter |
| `arch/x86_64/interrupt_entry.S` | Preserve registers and return from an interrupt |
| `include/kernel/platform.h` | Small contract between portable code and hardware |
| `kernel/main.c` | Shared console and command loop |
| `kernel/periodic.c` | Cooperative periodic dispatch with skipped-release accounting |
| `kernel/sensor.c` | Synthetic distance readings; debugger-visible task and sample |
| `kernel/distance_queue.c` | Four-slot copied-message FIFO with drop accounting |
| `kernel/controller.c` | Consume distance data and store a bounded teaching decision |
| `kernel/telemetry.c` | Versioned snapshots, CRC, bounded transmit queue |
| `scripts/telemetry.py` | Transport-independent binary decoder |
| `scripts/telemetry_view.py` | Live terminal view, JSONL recording, replay, serial adapter |
| `scripts/` | Build-time image generation and inspection |
| `tests/console.c` | Run the shared C code with simulated hardware on the host |

There is deliberately no third-party bootloader, host runtime, allocator,
preemptive scheduler, filesystem, or graphical UI in the kernel. The firmware and emulator
are external tools; we are not implementing a PC BIOS or an ARM firmware stack.

## Verify and inspect

```sh
make test                 # Shared console tests; not a boot test
make ARCH=x86_64 smoke    # Actual emulator boot and command test
make ARCH=arm64 smoke
readelf -h -l build/x86_64/gcc/kernel.elf
objdump -d build/x86_64/gcc/kernel.elf
```

`make check` checks ELF machine type, entry address, segment flags, and x86 disk
layout. These are **static** checks. Only `make smoke` tests execution in QEMU.
The ELF contains debugging symbols; `make debug` pauses QEMU and listens for
GDB on port 1234. In an architecture-capable GDB, load the matching ELF and run
`target remote :1234`, `hbreak kernel_main`, then `continue`.

Validation: the GNU x86_64 build, static image checks, hosted console tests, and
QEMU command smoke test passed. GDB checks also confirmed timer wakeup from HLT,
tick-counter advancement, return, and repeated IRQ delivery. Periodic task tests
cover phase and missed releases; GDB confirmed the first two synthetic samples
at tick-derived 100 and 200 ms in foreground context. ARM64 compilation
and execution, plus LLVM builds, remain unverified with the available tools.
Queue/controller tests cover overflow and stale data; QEMU/GDB checks confirmed
normal consumption, saturation while the consumer was disabled, and a stale
FIFO sample producing a zero-speed decision when it resumed.
Telemetry tests compare the C encoder with the Python decoder. QEMU produced
valid live packets and changing controller decisions; recording/replay, file
following, and pseudo-terminal serial input were checked. Real serial hardware
and the Jetson port remain untested.

## Learning milestones

1. **Firmware to C:** inspect the boot path, stack, and direct serial I/O.
2. **Interrupts:** trace timer entry/return and sleeping idle; later add UART IRQs
   and more informative exception diagnostics.
3. **Periodic tasks:** schedule one synthetic sensor and measure task releases.
4. **Messages:** pass fixed-size, timestamped data through bounded queues.
5. **Observation:** record framed telemetry, then build a viewer and replay it.
6. **Rover simulation:** close the sensor/controller/actuator loop in a 2D world.
7. **ARM64 and Orin Nano:** implement clock/interrupt and board support, preserving
   controller interfaces and host-side tools.

Firmware memory maps, device discovery, memory ownership, UEFI handoff, and fault
handling enter as required by those platform lessons. Process isolation and user
programs are later extensions rather than prerequisites for the first robot.

We will add each milestone with an explanation, a small experiment, and a
checkable result. Do not treat the later list as already implemented support.
