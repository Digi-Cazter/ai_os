#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <kernel/controller.h>
#include <kernel/distance_queue.h>
#include <kernel/sensor.h>

static void publish(uint64_t time, uint32_t distance, uint32_t valid)
{
    struct distance_sample sample = {
        .sampled_at_ms = time, .distance_mm = distance, .valid = valid,
    };
    assert(distance_queue_push(&sensor_queue, &sample));
}

int main(void)
{
    struct distance_queue queue;
    struct distance_sample sample = { .sequence = 99 };
    distance_queue_init(&queue);
    assert(!distance_queue_pop(&queue, &sample) && sample.sequence == 99);
    for (uint32_t i = 1; i <= DISTANCE_QUEUE_CAPACITY; ++i) {
        sample.sequence = i;
        assert(distance_queue_push(&queue, &sample));
    }
    sample.sequence = 100; /* Mutating the source must not mutate queued copies. */
    assert(!distance_queue_push(&queue, &sample));
    assert(queue.count == 4 && queue.dropped == 1 && queue.high_water == 4);
    assert(queue.published == 4);
    for (uint32_t i = 1; i <= 2; ++i) {
        assert(distance_queue_pop(&queue, &sample));
        assert(sample.sequence == i);
    }
    for (uint32_t i = 5; i <= 6; ++i) {
        sample.sequence = i;
        assert(distance_queue_push(&queue, &sample));
    }
    for (uint32_t i = 3; i <= 6; ++i) {
        assert(distance_queue_pop(&queue, &sample));
        assert(sample.sequence == i);
    }
    assert(queue.count == 0 && queue.consumed == 6 && queue.published == 6);

    sensor_init(0);
    controller_init(0);
    periodic_dispatch(&controller_task, 50);
    assert(controller_state.reason == CONTROL_WAITING);
    assert(controller_state.target_speed_mm_s == 0);
    periodic_dispatch(&sensor_task, 100);
    assert(sensor_queue.count == 1 && sensor_queue.slots[sensor_queue.head].sequence == 1);
    periodic_dispatch(&controller_task, 100);
    assert(sensor_queue.count == 0 && controller_state.processed_samples == 1);
    assert(controller_state.last_sample.sequence == 1);
    assert(controller_state.reason == CONTROL_CLEAR && controller_state.target_speed_mm_s == 200);
    controller_task_run(350); /* Age exactly 250 ms is still accepted. */
    assert(controller_state.reason == CONTROL_CLEAR);
    controller_task_run(351); /* No new input: check freshness again. */
    assert(controller_state.reason == CONTROL_STALE && controller_state.target_speed_mm_s == 0);

    publish(400, 700, 1);
    controller_task_run(400);
    assert(controller_state.reason == CONTROL_OBSTACLE && controller_state.target_speed_mm_s == 0);
    publish(450, 701, 1);
    controller_task_run(450);
    assert(controller_state.reason == CONTROL_CLEAR && controller_state.target_speed_mm_s == 200);
    publish(500, 2000, 0);
    controller_task_run(500);
    assert(controller_state.reason == CONTROL_INVALID && controller_state.target_speed_mm_s == 0);
    publish(1000, 2000, 1); /* Future timestamp fails freshness check. */
    controller_task_run(600);
    assert(controller_state.reason == CONTROL_STALE);
    publish(UINT64_MAX - 49, 2000, 1);
    controller_task_run(50);
    assert(controller_state.reason == CONTROL_CLEAR && controller_state.sample_age_ms == 100);

    sensor_init(0); /* Deliberately stall the consumer and fill the queue. */
    controller_init(0);
    for (uint64_t time = 100; time <= 500; time += 100)
        periodic_dispatch(&sensor_task, time);
    assert(sensor_reading.sequence == 5 && sensor_queue.count == 4 && sensor_queue.dropped == 1);
    controller_task_run(500);
    assert(sensor_queue.count == 3); /* At most one sample consumed per run. */
    assert(controller_state.last_sample.sequence == 1); /* FIFO, not latest-value. */
    assert(controller_state.reason == CONTROL_STALE);
    puts("PASS: copy/FIFO/wrap/full queue, producer-consumer, freshness, decisions, bounded work");
    return 0;
}
