#ifndef KERNEL_TELEMETRY_H
#define KERNEL_TELEMETRY_H

#include <kernel/periodic.h>

enum { TELEMETRY_FRAME_SIZE = 124, TELEMETRY_CAPACITY = 2 };

struct telemetry_state {
    unsigned char frames[TELEMETRY_CAPACITY][TELEMETRY_FRAME_SIZE];
    uint32_t head, tail, count, offset, sequence;
    uint64_t dropped;
};

extern struct periodic_task telemetry_task;
extern struct telemetry_state telemetry_state;
void telemetry_init(uint64_t now_ms);
void telemetry_capture(uint64_t now_ms);
void telemetry_drain(void);

#endif
