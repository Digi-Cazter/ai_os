# Lesson 5 — observe the running kernel without stopping it

We now capture state snapshots in the foreground, send framed bytes through a
second virtual serial port, and decode them in a host tool. The host tool draws
a live terminal view, records JSONL, and replays recordings. Python's standard
library is the only host dependency. This is the first observation tool, not a
rover physics simulator or a full task-event trace.

## Launch the live view

Exit old GDB and QEMU sessions so the new kernel and its ELF match.
In terminal 1, from the project directory:

```sh
make ARCH=x86_64 observe
```

This boots normally with console on COM1 and binary telemetry on COM2. QEMU
writes COM2 to `build/x86_64/gcc/telemetry.bin`. Each launch starts a new raw
file; use JSONL recording to preserve a session.

In terminal 2:

```sh
python3 scripts/telemetry_view.py \
  --input build/x86_64/gcc/telemetry.bin --follow --label 'QEMU x86_64'
```

The terminal display updates distance, recent received readings, queue usage,
the last controller decision, and loss counters. In a terminal it redraws in
place. Redirected output, or `--plain`, prints one line per snapshot instead.

The synthetic pattern reaches 700 mm at sample 14, so the controller's reason
becomes OBSTACLE and target speed becomes 0. The pattern resets after 500 mm,
and the controller returns to CLEAR. No motor or physical rover is moving.

Queue count is normally zero in these snapshots: the sensor publishes, then
the controller consumes, then telemetry captures state. High-water is normally
one and proves the queue was occupied. Snapshots can miss intermediate states;
we do not invent per-event history between them.

Press Ctrl-C to exit the viewer. It does not stop the kernel. Exit QEMU with
Ctrl-a, then x. Stop/restart the viewer for each guest launch so recording
sessions have an unambiguous start.

## Record and replay the same values

Choose a NEW JSONL filename (existing recordings are refused, not overwritten):

```sh
python3 scripts/telemetry_view.py \
  --input build/x86_64/gcc/telemetry.bin --follow \
  --record build/rover-session.jsonl --label 'QEMU x86_64'
```

After stopping that viewer, replay it without QEMU:

```sh
python3 scripts/telemetry_view.py --replay build/rover-session.jsonl
python3 scripts/telemetry_view.py --replay build/rover-session.jsonl --speed 4
```

Replay uses recorded HOST receive intervals, scaled by `--speed`. Target
timestamps remain unchanged. If you decode an already-complete raw file, its
packets arrive in a batch on the host; recording it does not reconstruct their
original receive timing. Use `--follow` during the run to capture live timing.

Each recording has a host-generated session UUID, a user-provided source label,
and UTC session-start metadata. Snapshots carry target time, host UTC receive
time, host monotonic receive time, and decoder loss/error counters. These clocks
are distinct; the tool does not pretend they are synchronized.

The receiver can infer a reset from time/sequence reversal or file truncation,
but v1 has no target-generated boot identity or firmware build identity.
Automatic reset inference is not infallible. A sequence wrap is supported;
source identity is a label, not an attestation from the device. Restart the
viewer explicitly between guest boots/rebuilds. Wire-level boot/build metadata
is a future protocol extension.

## What the kernel does

Read [telemetry.c](../kernel/telemetry.c):

1. A 10 Hz foreground task captures a state snapshot AFTER control dispatch.
2. It encodes each field into a preallocated frame and adds a checksum.
3. Two fixed slots hold pending frames. Full means drop the newest snapshot,
   count the drop, and return immediately.
4. Each foreground-loop iteration attempts one short serial write. If the UART
   is not ready, it returns zero immediately. A partially sent frame stays queued.

No telemetry encoding or serial output runs in the timer interrupt. Queue and
controller state have a consistent foreground snapshot because those tasks do
not execute concurrently with capture. The ISR only changes the timer counter.

The transport adapter in [platform.c](../arch/x86_64/platform.c) checks the second
UART's empty-transmit-FIFO flag and writes at most 16 bytes. It does not spin
until ready. Empty-FIFO permission lets us fill that known 16-byte FIFO once;
checking the flag after each byte instead would unnecessarily restrict progress.

The frame is 124 bytes. At 10 Hz that is 1240 data bytes/second, about 12400
serial bits/second with 8N1 framing, within a 115200 baud UART's line capacity.
Foreground servicing, blocking console output, and host/backend behavior can
still create delays. Line capacity is not a deadline guarantee.

The telemetry state occupies 280 bytes on this x86 build, including two frames
and queue metadata. The periodic task has its own storage. Check actual types
with `p sizeof(telemetry_state)` and `p sizeof(telemetry_task)` in GDB; use
`make size` for whole-kernel costs.

Acceptance by a UART is not proof that a host received the bytes. The protocol
has no delivery acknowledgments. Disconnected or absent readers may lose data
without increasing the kernel's queue-drop counter. Sequence gaps, checksums,
and receive silence are additional evidence, not a complete delivery guarantee.

## Wire protocol v1

Every multibyte integer uses little-endian byte order. The packet contains no
native C padding, pointers, or floating-point values.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 2 | Magic bytes `a5 5a` |
| 2 | 1 | Version: 1 |
| 3 | 1 | Kind: 1 (state snapshot) |
| 4 | 2 | Payload length: 104 |
| 6 | 4 | Snapshot attempt sequence |
| 10 | 8 | Snapshot target time in milliseconds |
| 18 | 104 | Snapshot fields |
| 122 | 2 | CRC-16/CCITT-FALSE |

The CRC covers offsets 2 through 121, excluding magic and the CRC itself.
Parameters: polynomial `0x1021`, initial value `0xffff`, no reflection, no final
XOR. It detects accidental corruption; it is not authentication.

The payload order and widths are listed in `FIELDS` in
[telemetry.py](../scripts/telemetry.py), matching explicit writes in the C
encoder: current sensor data, controller's sample/decision time and age, speed
and reason, queue count/high-water/publications/consumptions/drops, skipped
sensor/controller releases, and telemetry drops.

The snapshot sequence increments for EVERY capture attempt, including rejected
ones. A later successfully received snapshot can expose a gap. The receiver
cannot count losses before its first packet or after its last packet. Kernel
drop counts only refer to its two-slot telemetry queue, not all possible losses.

The host decoder searches for magic, bounds length to the supported schema,
checks the CRC, and resynchronizes after bad input. Arbitrary chunk boundaries
are normal: one host read need not contain exactly one frame. A partial final
frame at shutdown is reported, not misrepresented as a complete measurement.

## Read the display accurately

* Distance and speed are synthetic measurement / stored command, not motion.
* The history is received sensor values from snapshots, not every sensor event.
* Decision sample age is age AT THAT DECISION. `decision_at_ms` tells you when
  the controller ran. If the controller is disabled, its old CLEAR decision can
  remain visible while that decision time stops advancing.
* Queue drops are sensor publications rejected by the four-slot sensor queue.
* Telemetry drops are rejected observation snapshots, a different queue.
* Skipped releases count scheduler overload/disabled-task catch-up, not measured
  worst-case execution time. The tick clock cannot measure sub-tick work.
* If no packets arrive for a second of host time, the viewer reports silence and
  target status unknown. A debugger pause, halted guest, or disconnected transport
  can all produce silence. It does not infer a target diagnosis from that alone.

There is no CPU-utilization or power chart yet; we have not instrumented those
metrics. Observing HLT and counting packets is not a substitute for measurement.

## Combine the viewer with GDB

Use `make ARCH=x86_64 observe-debug` instead of `observe`. This opens COM2 as
usual but pauses the CPU and enables GDB on port 1234. Use three terminals:
QEMU, viewer, and GDB. The viewer reports silence until you continue the guest.

Repeat Lesson 4's consumer-stall experiment and watch queue occupancy rise:

```gdb
target remote localhost:1234
hbreak *controller_task_run
continue
set var controller_task.period_ms = 0
disable
continue
```

The sensor runs while the controller is disabled. After the queue fills, the
sensor-drop counter rises. To restore the consumer, press Ctrl-C in GDB, set
`controller_task.period_ms` to 50, and continue. Watch stale-data stop decisions
while the queue drains. Pausing the whole CPU and disabling only one task are
different experiments.

## Keep the same tool for hardware

The decoder consumes bytes, not QEMU APIs. A POSIX serial adapter is included:

```sh
python3 scripts/telemetry_view.py --serial /dev/ttyUSB0 \
  --record build/hardware-session.jsonl --label 'Orin Nano'
```

This is the intended usage AFTER the board emits our protocol on a configured
UART. It is not implemented Jetson board support. Choose the actual device and
electrical connection when doing that hardware lesson. ARM64 currently has
neither its clock nor its telemetry UART configured; its adapter returns zero.

The same decoder, record schema, and display can remain while we replace the
transport or kernel board drivers. Full task timelines, board/build discovery,
networking, and richer graphical views can be added above this first boundary.

## Checks performed

`make test` now generates packets with the ACTUAL C encoder and decodes them
with Python. It checks fragmented input at every split, CRC failure and
resynchronization, invalid lengths, sequence gaps/wrap/reset, queue backpressure,
partial writes, unchanged queued copies, recording/replay, and refusal to
overwrite recordings. Existing kernel tests still run.

QEMU produced 24 live packets with valid checksums, CLEAR and OBSTACLE decisions,
and zero telemetry queue drops during that run. The console still accepted
commands, and Ctrl-a x exited the two-port setup. Actual packets were recorded
and replayed with matching values. Additional checks exercised growing-file
input, truncation/new host session, serial input via a pseudo-terminal, and
interactive terminal rendering/cursor restoration. Physical serial hardware
and Jetson support remain untested.
