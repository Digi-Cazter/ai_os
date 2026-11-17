#ifndef KERNEL_CONTROLLER_H
#define KERNEL_CONTROLLER_H

#include <kernel/messages.h>
#include <kernel/periodic.h>

enum controller_reason {
    CONTROL_WAITING = 0,
    CONTROL_CLEAR,
    CONTROL_OBSTACLE,
    CONTROL_STALE,
    CONTROL_INVALID,
};

struct controller_state {
    struct distance_sample last_sample;
    uint64_t processed_samples;
    uint64_t sample_age_ms;
    uint32_t have_sample;
    uint32_t target_speed_mm_s;
    enum controller_reason reason;
};

extern struct periodic_task controller_task;
extern struct controller_state controller_state;
void controller_init(uint64_t now_ms);
void controller_task_run(uint64_t now_ms);

#endif
