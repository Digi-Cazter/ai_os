# Lesson 4 — a bounded queue connects two tasks

The synthetic sensor now copies readings into a four-slot FIFO queue. A second
foreground task consumes one reading at a time and stores a simple speed
decision. This is the first message-passing link in our robotics runtime.

```text
Sensor task (10 Hz) -> four distance-message slots -> controller task (20 Hz)
                                                       |
                                            target speed stored in RAM
```

There is no motor driver, rover physics, steering control, network transport,
or live viewer yet. A decision in RAM is not physical actuator control.

## The pieces

* [messages.h](../include/kernel/messages.h) gives the measurement its shared
  meaning: sample timestamp, sequence, millimeters, and validity.
* [distance_queue.h](../include/kernel/distance_queue.h) defines fixed storage,
  read/write positions, count, and counters. Its queue is debugger-visible as
  `sensor_queue`.
* [distance_queue.c](../kernel/distance_queue.c) implements push and pop without
  waiting or allocation. A successful push copies the sample, not its address.
* [sensor.c](../kernel/sensor.c) produces and publishes one reading per callback.
* [controller.c](../kernel/controller.c) consumes at most one reading per callback
  and decides a target speed. State is visible as `controller_state`.

The controller runs at 20 Hz (50 ms period), faster than the 10 Hz sensor.
The main loop dispatches the sensor first, then the controller, so a sample
can be consumed in the same foreground iteration when both are due. The clock
is refreshed between tasks so the controller start time reflects that ordering.

## A ring buffer has finite capacity

```text
slots: [0] [1] [2] [3]
head:  next occupied slot to read
tail:  next slot to write
count: number of occupied slots
```

After index 3, an index wraps to 0. The count distinguishes empty from full,
because head and tail can be equal in either case. Pop does not erase slot
bytes; count and indices determine which slots are occupied. Old bytes are not
new messages. Initializing the queue resets metadata without clearing every slot.

On the current x86 build, the queue occupies 136 bytes, including four 24-byte
C sample representations and its indices/counters. Use `p sizeof(sensor_queue)`
to inspect the current build. C padding is one reason we will explicitly encode
the later telemetry protocol instead of sending raw struct bytes.

This first queue has **one consumer**. Consuming a sample removes it. Two
consumers would compete for messages, not each receive every message. Broadcast
publish/subscribe requires another design lesson.

The sensor and controller both execute sequentially in foreground code. The
timer ISR never accesses this queue. It therefore needs no locks in our current
single-CPU cooperative design. Do not use it from an interrupt handler or a
second CPU without changing its synchronization contract.

## Full means reject and count

When count is 4, a new push returns 0, increments `dropped`, and leaves the
existing four messages intact. The producer does not wait or retry. Its local
`sensor_reading` still advances, so a generated sample and a successfully
published sample are distinct events.

The counters have explicit meanings:

| Counter | Meaning |
| --- | --- |
| `published` | Successfully queued samples |
| `consumed` | Successfully removed samples |
| `dropped` | New samples rejected because the queue was full |
| `high_water` | Maximum observed occupied-slot count since initialization |

FIFO preserves old samples. That makes age checking important after a consumer
stall: a full queue does not imply that its data is fresh.

## A deliberately small controller

Each run attempts one pop, then makes a decision using the latest sample it
has consumed. Even with no new input, it rechecks the held sample's age.

| Reason | Condition | Target speed |
| --- | --- | --- |
| `CONTROL_WAITING` | No sample consumed yet | 0 |
| `CONTROL_INVALID` | Sample validity is false | 0 |
| `CONTROL_STALE` | Age exceeds 250 ms | 0 |
| `CONTROL_OBSTACLE` | Valid, fresh distance is at most 700 mm | 0 |
| `CONTROL_CLEAR` | Valid, fresh distance is greater than 700 mm | 200 mm/s |

The thresholds are teaching constants, not a validated stopping-distance model.
There is no actual motion. Age is measured in target-clock milliseconds;
future timestamps also fail the unsigned age check. The unit tests include
the age boundary and timestamp wrap. The system assumes producer timestamps
use this same target clock; cross-computer clock conversion is not implemented.

## Watch the first sample get consumed

Exit old GDB/QEMU sessions and start `make ARCH=x86_64 debug` in terminal 1.
In terminal 2, load the new ELF, then enter:

```gdb
target remote localhost:1234
hbreak *controller_task_run
continue
p sensor_queue.count
p controller_state
```

The first controller release is 50 ms after initialization, before the first
sensor release. Queue count should be 0. Execute the callback:

```gdb
finish
p controller_state.reason
p controller_state.target_speed_mm_s
```

Expect `CONTROL_WAITING` and speed 0. Continue to the next entry:

```gdb
continue
p sensor_queue.count
p sensor_queue.slots[sensor_queue.head]
```

At about 100 ms, expect one queued sample: sequence 1, distance 2000 mm.
The controller has not consumed it yet. Complete the callback:

```gdb
finish
p sensor_queue.count
p sensor_queue.consumed
p controller_state
```

Expect count 0, consumed 1, processed_samples 1, and target speed 200 mm/s.
The last sample remains in `controller_state`, independently of the queue slot.

## Deliberately stall the consumer

With the normal controller stopped after its first successful consumption,
disable its future dispatches by setting its period to zero. The ISR and
sensor continue; we are not pausing the entire CPU:

```gdb
set var controller_task.period_ms = 0
disable
hbreak *sensor_task_run if sensor_queue.dropped > 0
continue
p sensor_queue
p sensor_reading
```

The conditional breakpoint stops at a sensor entry AFTER a previous publication
has been dropped. Expect count 4 and dropped at least 1. The sensor sequence
has advanced beyond the newest successfully queued sample. Merely pausing
everything in GDB would not demonstrate a producer outpacing a consumer.

Restore the consumer and stop at its next invocation:

```gdb
set var controller_task.period_ms = 50
disable
hbreak *controller_task_run
continue
finish
p sensor_queue.count
p controller_state
```

Only one item is removed, leaving 3. The controller sees an old FIFO sample
and stores a stale-input stop decision. Its task also records skipped releases
after being disabled. In this experiment, debugger pauses distort wall time;
inspect target timestamps and counters rather than drawing timing guarantees.

## What was checked

`make test` covers empty/full queues, value-copy independence, FIFO ordering,
ring-index wrap, counters, normal production/consumption, no-input waiting,
freshness expiry without new input, invalid data, distance thresholds, future
timestamps, timestamp wrap, and one-pop-per-run behavior. Existing console and
periodic tests still run.

The actual QEMU guest was also checked with GDB: empty queue at the first
controller run, sequence 1 publication and consumption, a full queue and a
counted rejected sample with the consumer disabled, and a stale-input stop
decision after restoring it. The command smoke test passed as well.

[Lesson 5](05-live-telemetry.md) now encodes these observations into a framed
telemetry stream and shows them in a host tool without stopping the CPU for
every measurement. Transient queue states between snapshots can be missed;
the high-water and cumulative counters supply additional evidence.
