#include <kernel/controller.h>
#include <kernel/distance_queue.h>

enum {
    CONTROLLER_PERIOD_MS = 50, /* 20 Hz consumer, 10 Hz sensor producer. */
    MAX_SAMPLE_AGE_MS = 250,
    STOP_DISTANCE_MM = 700,
    CRUISE_SPEED_MM_S = 200,
};

struct periodic_task controller_task;
struct controller_state controller_state;

void controller_init(uint64_t now_ms)
{
    controller_state = (struct controller_state){ .reason = CONTROL_WAITING };
    controller_task = (struct periodic_task) {
        .period_ms = CONTROLLER_PERIOD_MS,
        .last_release_ms = now_ms,
        .run = controller_task_run,
    };
}

void controller_task_run(uint64_t now_ms)
{
    /* At most ONE pop per dispatch: bounded work, even with a full queue.
     * Keep the last sample between arrivals, but recheck its age EVERY run. */
    if (distance_queue_pop(&sensor_queue, &controller_state.last_sample)) {
        controller_state.have_sample = 1;
        ++controller_state.processed_samples;
    }

    controller_state.target_speed_mm_s = 0;
    if (!controller_state.have_sample) {
        controller_state.reason = CONTROL_WAITING;
        return;
    }
    controller_state.sample_age_ms = now_ms - controller_state.last_sample.sampled_at_ms;
    if (!controller_state.last_sample.valid)
        controller_state.reason = CONTROL_INVALID;
    else if (controller_state.sample_age_ms > MAX_SAMPLE_AGE_MS)
        controller_state.reason = CONTROL_STALE;
    else if (controller_state.last_sample.distance_mm <= STOP_DISTANCE_MM)
        controller_state.reason = CONTROL_OBSTACLE;
    else {
        controller_state.reason = CONTROL_CLEAR;
        controller_state.target_speed_mm_s = CRUISE_SPEED_MM_S;
    }
    /* This is a teaching decision stored in RAM, not a motor driver, vehicle
     * model, or validated stopping-distance calculation. */
}
