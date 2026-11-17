#include <kernel/sensor.h>
#include <kernel/distance_queue.h>

struct periodic_task sensor_task;
struct distance_sample sensor_reading;

void sensor_init(uint64_t now_ms)
{
    sensor_reading = (struct distance_sample){0};
    distance_queue_init(&sensor_queue); /* Initialize the stream before tasks run. */
    sensor_task = (struct periodic_task) {
        .period_ms = 100, /* 100 ms/sample = 10 samples per second (10 Hz). */
        .last_release_ms = now_ms,
        .run = sensor_task_run,
    };
}

void sensor_task_run(uint64_t now_ms)
{
    /* Deterministic teaching pattern: 2000, 1900, ... 500 mm, then repeat.
     * This is synthetic data, not a distance computed from rover physics. */
    uint32_t step = sensor_reading.sequence % 16;
    sensor_reading.sampled_at_ms = now_ms;
    sensor_reading.distance_mm = 2000 - step * 100;
    ++sensor_reading.sequence;
    sensor_reading.valid = 1;
    (void)distance_queue_push(&sensor_queue, &sensor_reading);
    /* Full queue: the reading still exists locally, but publication is dropped
     * and counted. Never wait for the consumer or retry indefinitely. */
}
