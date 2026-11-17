#ifndef KERNEL_SENSOR_H
#define KERNEL_SENSOR_H

#include <kernel/periodic.h>
#include <kernel/messages.h>

/* Debugger-visible state, written only by the foreground task. This is a
 * local C representation, not a serialized telemetry packet. Each reading
 * is also copied into sensor_queue for a single foreground consumer. */
extern struct periodic_task sensor_task;
extern struct distance_sample sensor_reading;

void sensor_init(uint64_t now_ms);
void sensor_task_run(uint64_t now_ms);

#endif
