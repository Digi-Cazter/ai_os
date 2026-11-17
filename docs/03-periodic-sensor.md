# Lesson 3 — one periodic sensor task

We now have a synthetic sensor that runs every 100 milliseconds, nominally
10 Hz. This is the first task in our robotics runtime. There is no rover physics,
message queue, controller, or streaming telemetry yet. Its output is stored in a
debugger-visible `sensor_reading` structure.

## Two different rates

```text
PIT interrupt:          every ~10 ms -> increment timer_ticks -> return
Foreground dispatcher: elapsed time >= 100 ms -> run sensor_task_run
```

Frequency is the inverse of period: 100 ms is 0.1 seconds, so 1 / 0.1 = 10 Hz.
The timer runs at about 100 Hz; the task runs at about 10 Hz. The task is not
called inside the interrupt handler. Ten timer ticks make one task period.

`platform_time_ms()` translates x86's tick counter into millisecond units. The
clock is coarse (10 ms resolution) and based on the approximate PIT rate.
Millisecond units do not imply millisecond accuracy. ARM64 returns "clock not
available" until we implement its timer; its sensor task intentionally stays
disabled instead of using loop iterations as pretend time.

## Inspect the small pieces

1. [periodic.h](../include/kernel/periodic.h) defines one task's state: period,
   callback, last release, dispatch count, missed releases, start, and lateness.
2. [periodic.c](../kernel/periodic.c) decides whether a task is due and invokes
   its callback in foreground context. There is no task thread or separate stack.
3. [sensor.c](../kernel/sensor.c) initializes one task and generates measurements.
4. [main.c](../kernel/main.c) checks for due work on each loop iteration, before
   checking serial input. Continuous incoming characters do not bypass this
   check, though blocking output or other long foreground work can delay it.

The first release is 100 ms after task initialization. The synthetic readings
are 2000, 1900, 1800, ... 500 millimeters, then repeat. This is a teaching
pattern, not evidence that a rover is moving. The timestamp is the actual
dispatch-start time provided to the callback. Validity starts false until the
first completed sample; the sample sequence counts callbacks that produced data.

## Due is not the same as executing

If the task starts on time, release and start match. If the foreground loop is
late, they differ. Suppose initialization was at time 0 and we next dispatch at
450 ms. The releases at 100, 200, 300, and 400 ms have all passed.

Our policy executes **one** callback, assigns it the latest release at 400 ms,
and counts the other three as missed releases. Start is 450 ms and lateness
relative to that latest release is 50 ms. The next release stays at 500 ms,
not 550 ms: we preserve phase rather than resetting the schedule to "now."
The lateness value alone does not reveal skipped periods; inspect both fields.

`dispatch_count` increments before the callback starts, not after it finishes.
It is a dispatch counter, not a completed-work or success counter. Callbacks run
to completion on one shared foreground stack. A slow callback delays everything
else; this is not preemptive scheduling or a hard real-time timing guarantee.

## First GDB experiment

Exit the old GDB and QEMU sessions, then restart with matching new symbols.
In terminal 1:

```sh
make ARCH=x86_64 check
make ARCH=x86_64 debug
```

In terminal 2:

```sh
gdb -q build/x86_64/gcc/kernel.elf
```

Then:

```gdb
target remote localhost:1234
hbreak *sensor_task_run
continue
p/d sensor_task.period_ms
p/d sensor_task.dispatch_count
p/d sensor_reading.sequence
bt
```

At the first callback entry, expect period 100, dispatch count 1, and sample
sequence 0. The task is due, but has not generated its first sample yet. The
backtrace should lead back to the foreground main loop, not `timer_interrupt`.
The optimized dispatcher may not appear as its own frame because the compiler
can turn its final callback call into a tail jump.

Prove that interrupts are enabled while task code executes:

```gdb
p/x $eflags & 0x200
```

Expect `0x200`, unlike the ISR context from Lesson 2. Next execute the task:

```gdb
finish
p sensor_reading
p sensor_task
```

Expect sequence 1, distance 2000 mm, validity 1, and matching release/start
times if the foreground loop dispatched on its tick. Continue to the next
entry and complete it:

```gdb
continue
finish
p sensor_reading
```

Expect sequence 2 and distance 1900 mm. Inspect the timestamps rather than
assuming exact host wall-clock spacing: debugger stops distort time progression
and interrupts can coalesce. An observed 100 ms timestamp difference establishes
tick-based scheduling in that run, not measured physical latency.

## Reproducible checks

`make test` covers just-before-release, exact release, duplicate polling at the
same time, delayed dispatch, skipped releases, phase preservation, sample pattern,
unsigned clock wrap, and disabled tasks. It also runs the existing console
tests. `make check` checks the image, and `make size` reports footprint.

The guest was also checked in QEMU/GDB: first two completed samples at
tick-derived 100 and 200 ms, foreground stack, interrupt-enable flag set, and
the existing command smoke test passing. These checks do not establish a
worst-case execution time or power measurement. The current x86 build has
1802 initialized bytes and a 24672-byte physical address span; future builds
should be measured again rather than treating those values as budgets.

## Next boundary: a message queue

The original version of this lesson only overwrote one latest-reading structure.
In the current code, [Lesson 4](04-bounded-messages.md) also copies each sample
into a four-slot queue and dispatches a controller task to consume it. The
`sensor_reading` structure remains useful for inspecting the producer. Queue
state and the consumer's last sample are separate objects; producer progress
does not establish successful publication or consumption.
