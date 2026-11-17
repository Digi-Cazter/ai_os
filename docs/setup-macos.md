# macOS setup — Intel and Apple Silicon

The development computer can be a Mac while our kernel still targets an
x86_64 virtual PC. The host CPU and kernel target are separate choices. QEMU
emulates the target CPU on Apple Silicon; no Rosetta shell is required.

Use Homebrew's GNU bare-metal ELF toolchain so our existing lesson commands and
`build/x86_64/gcc/` paths stay the same. Apple's built-in compiler and linker
normally produce macOS binaries; the kernel needs ELF.

This guide was checked against the project and upstream package listings.
The current test environment is Linux; native macOS build/run and physical
serial input have not been verified here. Homebrew support/prebuilt packages
vary by macOS version, particularly on older Intel Macs. Consult its package
pages if it reports an unsupported configuration.

## Install host tools

If Apple's Command Line Tools are not already installed:

```sh
xcode-select --install
```

Finish the installation before continuing. It provides `make` and the host C
compiler used for tests.

Install Homebrew using its [official instructions](https://brew.sh/) and follow
the printed "Next steps" to put `brew` on PATH. The normal prefix is
`/opt/homebrew` on Apple Silicon and `/usr/local` on Intel. See
[Homebrew installation documentation](https://docs.brew.sh/Installation).

```sh
brew --version
brew install python qemu x86_64-elf-gcc x86_64-elf-binutils x86_64-elf-gdb
```

Official package references:
[compiler](https://formulae.brew.sh/formula/x86_64-elf-gcc),
[binutils](https://formulae.brew.sh/formula/x86_64-elf-binutils),
[cross-debugger](https://formulae.brew.sh/formula/x86_64-elf-gdb), and
[QEMU](https://formulae.brew.sh/formula/qemu).

Verify the executable names:

```sh
command -v make python3 x86_64-elf-gcc x86_64-elf-ld x86_64-elf-gdb qemu-system-x86_64
```

Each name should resolve to a path. No pip packages are needed for our viewer.

## Build and boot

Copy or check out the SOURCE project on the Mac. Use your Mac's actual path,
not the Linux `/home/donavan/...` directory:

```sh
cd /path/to/ai_os
make ARCH=x86_64 check
make test
make ARCH=x86_64 size
make ARCH=x86_64 run
```

The Makefile detects `Darwin` and selects `x86_64-elf-gcc`/`x86_64-elf-ld`.
Host tests separately use native `cc`; their executables run on your Mac.
Bring source rather than reusing Linux-generated objects. Preserve recordings
separately: `make clean` removes all of `build/`, including recordings there.

Exit QEMU with Ctrl-a followed by x. Leave our default software-emulation
configuration in place; do not add `-accel hvf` to x86 emulation on an ARM Mac.
After exiting other QEMU sessions, `make ARCH=x86_64 smoke` tests guest boot and
commands automatically.

## Live telemetry

Exit the previous guest. Terminal 1, in the project directory:

```sh
make ARCH=x86_64 observe
```

Terminal 2, in the SAME project directory:

```sh
python3 scripts/telemetry_view.py \
  --input build/x86_64/gcc/telemetry.bin --follow --label 'QEMU on macOS'
```

The console stays in terminal 1; the viewer runs in terminal 2. Ctrl-C stops
the viewer without stopping the guest. This is the same telemetry format and
synthetic sensor as on Linux.

## Record and replay

With `make observe` running, replace the viewer command with:

```sh
python3 scripts/telemetry_view.py \
  --input build/x86_64/gcc/telemetry.bin --follow \
  --record build/mac-rover-session-1.jsonl --label 'QEMU on macOS'
```

Use a NEW output filename each time. After Ctrl-C, replay it:

```sh
python3 scripts/telemetry_view.py --replay build/mac-rover-session-1.jsonl
```

Existing Linux JSONL recordings can also be copied to the Mac and replayed
directly. Their timestamps remain observations from the original run.

## Debug using our GDB lessons

Exit the guest and viewer. Terminal 1:

```sh
make ARCH=x86_64 debug
```

Terminal 2:

```sh
x86_64-elf-gdb -q build/x86_64/gcc/kernel.elf
```

At the GDB prompt:

```gdb
target remote localhost:1234
hbreak *sensor_task_run
continue
layout split
p sensor_reading
```

Use `x86_64-elf-gdb` to start the cross-debugger; once inside it, the lesson
commands are unchanged. It connects to QEMU rather than attaching to a native
macOS process. Use the newly built ELF and symbols, not addresses copied from
another compiler build. Ctrl-L redraws the display; `tui disable` leaves TUI.

For telemetry alongside debugging, use `make observe-debug` and put the viewer
in a third terminal.

## Inspect ELF files

macOS's `otool` handles native Mach-O files. For our kernel, use cross-binutils:

```sh
x86_64-elf-readelf -h -l build/x86_64/gcc/kernel.elf
x86_64-elf-objdump -d build/x86_64/gcc/kernel.elf
od -An -tx1 -j510 -N2 build/x86_64/gcc/aios.img
```

The last command should still display the BIOS marker `55 aa`.

## Optional ARM64 toolchain

This is not needed for the current x86 interrupt/telemetry lessons:

```sh
brew install aarch64-elf-gcc aarch64-elf-binutils aarch64-elf-gdb
make ARCH=arm64 check
make ARCH=arm64 run
```

See Homebrew's
[ARM64 compiler](https://formulae.brew.sh/formula/aarch64-elf-gcc) and
[ARM64 debugger](https://formulae.brew.sh/formula/aarch64-elf-gdb).
Mac defaults select `aarch64-elf-gcc`/`aarch64-elf-ld`. This targets QEMU virt,
not Jetson Orin Nano. ARM64 still lacks our timer and telemetry implementation;
do not expect x86 live-data lessons there yet. ARM64 builds/execution remain
unverified in the current environment.

LLVM is an optional alternative (`brew install llvm lld`, `TOOLCHAIN=llvm`),
not the default route in this guide. Homebrew LLVM is keg-only; choose its
explicit tool paths or documented PATH setup. LLVM artifacts use
`build/<arch>/llvm/`, not `gcc/`.

## Future physical serial input

On macOS, serial ports commonly appear under `/dev/cu.*`:

```sh
ls /dev/cu.*
```

Once a board emits our protocol, the same viewer can read its port:

```sh
python3 scripts/telemetry_view.py --serial /dev/cu.usbserial-EXAMPLE \
  --record build/hardware-session-1.jsonl --label 'Orin Nano'
```

Replace the example device name. This describes the intended observation
interface, not implemented Jetson firmware support. Physical serial hardware
and native macOS serial behavior have not been tested here.

## Troubleshooting

* `brew` missing: follow Homebrew's shell/PATH instructions, then open a new
  terminal. Use the prefix belonging to your installation.
* Cross compiler/linker missing: install the named ELF packages. macOS `gcc`
  is often Apple Clang, and native `ld` is not our ELF linker.
* `gdb` missing: the installed command here is `x86_64-elf-gdb`.
* Telemetry file missing: run `make observe` first. Plain `make run` does not
  connect COM2 to that file. Both terminals must use the project directory.
* Disk-image lock: exit the older QEMU instance using the image.
* Wrong input format: `--replay` reads JSONL; `--input` reads binary packets.
