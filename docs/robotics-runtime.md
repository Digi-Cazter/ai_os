# A small robotics kernel and tools that follow it onto hardware

This is the project direction, not a list of implemented features. We have
boot code, a console, x86 timer interrupts, and cooperative sensor/controller
tasks connected by a four-slot distance queue. Versioned snapshots now feed a
host terminal viewer with recording/replay. Rover physics and Jetson board
support come next. Follow
[Lesson 3](03-periodic-sensor.md) for the first task and
[Lesson 4](04-bounded-messages.md) for messaging, then
[Lesson 5](05-live-telemetry.md) for the first live observation tool.

The goal is to learn each mechanism by implementing it, inspecting its state,
and measuring its cost. The first robot is a four-wheel rover with two
steerable front wheels and rear-wheel drive. Jetson Orin Nano is our first
planned physical computer; FPGA peripherals are a later extension.

## What "kernel-level ROS" means for this project

We will build a small ROS-inspired runtime with periodic tasks, typed messages,
bounded queues, and observable timing. It will not initially implement ROS 2
protocols or run ROS 2 packages. Interoperability would require a separate bridge.

The kernel supplies mechanisms: interrupts, clocks, scheduling, messaging,
device access, and trace events. Rover modules supply behavior: interpreting
sensors, choosing motion, and commanding steering and drive. These stay separate
in source even while our early single-address-space kernel runs everything with
kernel privilege. A module boundary does not provide memory or fault isolation.

No runtime allocation is needed for the first tasks and queues. Capacities,
message sizes, task periods, and overload policies are explicit. Small and
predictable operations are design intentions; timing guarantees require
measurement and analysis on the eventual target.

## One loop from the world back to the world

```text
Rover physics -> sensor model -> transport -> sensor adapter
                                               |
                                      timestamped measurement
                                               |
                                        controller task
                                               |
                                    speed + steering command
                                               |
Rover physics <- actuator model <- transport <- actuator adapter
```

QEMU emulates the computer; a host process simulates the rover and environment.
The controller consumes sensor messages, not simulator-specific objects.
Replacing simulated sensors with board drivers should preserve message meanings
and controller source. Drivers and boot code will change, and the code must be
rebuilt for the target. This does not promise binary compatibility.

Initially the simulator will exchange sensor/actuator packets through a virtual
device. That tests the control software and transport driver, not the electrical
behavior of an eventual I2C, SPI, encoder, or motor interface. Later peripheral
models and real hardware tests will exercise those lower layers.

## Keep the first scheduler inspectable

Start with one CPU and cooperative, run-to-completion task functions. Each task
has an explicit period; tasks do not block waiting for a sensor or transport.
Timer interrupts record time/work becoming due, then return. Foreground code
dispatches tasks. No scheduling callbacks or telemetry formatting in the ISR.

If a task overruns, record it and skip missed releases rather than building an
unbounded backlog. One slow cooperative task can delay the others; deadlines
are not guaranteed by this initial scheduler. Preemption is a separate lesson.

The clock interface must eventually expose a monotonic target time. Our current
100 Hz PIT tick is enough for an introductory periodic task, but cannot measure
sub-tick execution time. A task period is a scheduling request, not evidence of
measured execution latency. Start with a 10 Hz simulated sensor so its period
is comfortably larger than our first timer tick.

## Messages have defined meaning and bounded storage

The first demonstration passes one synthetic distance measurement from a sensor
task to a controller task. Its schema specifies units, timestamp meaning,
validity, and sequence number. A second message carries the controller output.

Every queue declares capacity and overflow behavior. For this first sensor
stream, reject the newest publication when full, increment a drop counter, and
let the consumer reject stale queued measurements using their timestamps.
Commands and events may need different policies; never silently inherit the
sensor queue's policy. Invalid/stale data must have defined controller behavior.

Publishing copies a fixed-size value into a preallocated queue. Transport
failures must not make a control task wait indefinitely. Trace/telemetry queues
may lose records under load, but expose their loss counts. We do not let a
disconnected viewer stop the rover tasks from running.

## A stable observation boundary

```text
Kernel/task events -> bounded telemetry queue -> transport -> host decoder
                                                        -> recording
                                                        -> visualization

Sources over time:
  QEMU guest / ARM guest / Orin board / recorded session
                      -> the same decoded event schema
```

The wire format is versioned and explicitly serialized, with fixed-width
fields and specified byte order. Never transmit native C structs or pointers:
padding, layout, and addresses are not portable interfaces. A framed record
needs type, length, session identity, sequence number, target timestamp, and
payload, plus an integrity check. Receivers impose a maximum record size and
can recover framing after corrupt data. We will choose and test the exact
encoding in its own lesson.

Target time and host receive time are different fields. Record clock frequency,
architecture/board identity, build identity, and event-schema version with each
session. A reboot starts a new session. Gaps in sequence numbers and queue-drop
counts identify missing observations instead of drawing apparently continuous
data. Console text and framed telemetry use separate logical channels.

Host transport adapters translate virtual serial, a physical serial connection,
or later networking into the same event stream. Readers consume that stream,
not a particular device path. Recording/replay uses the same decoded schemas.
Hardware need not send a rover picture: the viewer reconstructs state from
measurements and commands. The visualization runs on the development computer.

## Views to build around actual measurements

1. Task timeline: release, start, completion, lateness, and missed releases.
2. Topic flow: publisher, consumer, sequence, queue occupancy, drops, data age.
3. Rover view: pose, front-wheel angles, wheel speeds, obstacles, sensor readings.
4. Footprint and load: build-time code/RAM sizes, measured task time and sleep
   residency when a suitable clock and instrumentation exist.

Label simulated values, measured target values, and configured limits distinctly.
An emulator result is not a hardware execution-time or power measurement. These
tools can display the same metrics on each target without implying equal timing.

During GDB-driven rover experiments, coordinate simulation steps with guest
progress so a paused controller does not leave the simulated rover driving on.
Define that synchronization and communication timeout behavior before closing
the actuator loop. Recording and fault injection should be reproducible.

## Small lessons, each with an observable result

1. Periodic work: one 10 Hz synthetic sensor task; observe releases in GDB.
2. Messaging: one fixed-size queue and one consuming task; demonstrate overflow
   and stale-data handling in tests and in a running guest.
3. Telemetry: frame those events, decode and record them on the host. Exercise
   truncation, bad integrity checks, disconnection, and queue overflow.
4. First viewer: visualize sensor values, queue use, and task releases from both
   live and recorded data. No simulation physics is required for this lesson.
5. Rover loop: add simple 2D motion, rear-wheel drive, front Ackermann steering,
   encoders, heading, and range. First behavior: approach an obstacle and stop.
6. Platform transfer: ARM64 clock/interrupt support in QEMU, then investigate
   Orin Nano firmware handoff and board drivers. Reuse the schemas and viewer.
7. Hardware experiments: actual sensor and actuator interfaces; compare traces
   with simulation and investigate differences. FPGA peripherals can implement
   another device behind the same application-level messages.

We will explain and inspect each lesson before adding another. Full ROS 2
interoperability, GPU support, process isolation, networking, and a sophisticated
physics engine are separate projects, not prerequisites for the first rover.
