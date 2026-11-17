#ifndef KERNEL_PERIODIC_H
#define KERNEL_PERIODIC_H

#include <stdint.h>

/* A cooperative task: one foreground callback runs to completion at a time.
 * All times use the platform's monotonic millisecond clock. Period must be
 * nonzero; zero leaves a task disabled. State belongs to foreground code. */
struct periodic_task {
    uint64_t period_ms;
    uint64_t last_release_ms;
    uint64_t dispatch_count;
    uint64_t missed_releases;
    uint64_t last_start_ms;
    uint64_t last_lateness_ms;
    void (*run)(uint64_t now_ms);
};

void periodic_dispatch(struct periodic_task *task, uint64_t now_ms);

#endif
