#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <kernel/sensor.h>

int main(void)
{
    sensor_init(0);
    assert(!sensor_reading.valid);
    periodic_dispatch(&sensor_task, 99);
    assert(sensor_task.dispatch_count == 0);
    periodic_dispatch(&sensor_task, 100);
    assert(sensor_task.dispatch_count == 1);
    assert(sensor_reading.sequence == 1);
    assert(sensor_reading.distance_mm == 2000 && sensor_reading.valid);
    assert(sensor_reading.sampled_at_ms == 100);
    periodic_dispatch(&sensor_task, 100); /* A timestamp isn't a new release. */
    assert(sensor_task.dispatch_count == 1);

    periodic_dispatch(&sensor_task, 450); /* Releases at 200, 300, 400. */
    assert(sensor_task.dispatch_count == 2); /* One callback, not three. */
    assert(sensor_task.missed_releases == 2);
    assert(sensor_task.last_release_ms == 400);
    assert(sensor_task.last_start_ms == 450 && sensor_task.last_lateness_ms == 50);
    assert(sensor_reading.sequence == 2 && sensor_reading.distance_mm == 1900);
    periodic_dispatch(&sensor_task, 500); /* Preserve phase, not 450 + 100. */
    assert(sensor_task.dispatch_count == 3 && sensor_task.last_lateness_ms == 0);

    sensor_init(0);
    for (uint64_t i = 1; i <= 17; ++i) {
        periodic_dispatch(&sensor_task, i * 100);
        assert(sensor_reading.distance_mm == 2000 - ((i - 1) % 16) * 100);
    }
    sensor_init(UINT64_MAX - 49); /* Test unsigned elapsed time across wrap. */
    periodic_dispatch(&sensor_task, 49);
    assert(sensor_task.dispatch_count == 0);
    periodic_dispatch(&sensor_task, 50);
    assert(sensor_task.dispatch_count == 1 && sensor_task.last_release_ms == 50);

    sensor_task.period_ms = 0; /* Disabled task must not divide by zero. */
    periodic_dispatch(&sensor_task, 1000);
    assert(sensor_task.dispatch_count == 1);
    puts("PASS: releases, phase, missed work, synthetic samples, wrap, disabled task");
    return 0;
}
