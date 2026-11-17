/* Cross-language fixture: C's actual encoder -> Python's actual decoder. */
#include <assert.h>
#include <stdio.h>
#include <kernel/telemetry.h>
#include <kernel/platform.h>
#include <kernel/sensor.h>
#include <kernel/controller.h>

static unsigned char output[1024];
static unsigned position;
static unsigned accept_limit;

unsigned platform_telemetry_write(const unsigned char *bytes, unsigned length)
{
    unsigned count = length < accept_limit ? length : accept_limit;
    assert(position + count <= sizeof(output));
    for (unsigned i = 0; i < count; ++i)
        output[position++] = bytes[i];
    return count;
}

int main(int argc, char **argv)
{
    sensor_init(0);
    controller_init(0);
    periodic_dispatch(&sensor_task, 100);
    periodic_dispatch(&controller_task, 100);
    telemetry_init(0);
    telemetry_capture(100);
    sensor_reading.distance_mm = 9999; /* Captured frames must retain old values. */
    telemetry_capture(200);
    telemetry_capture(300); /* Full queue: no overwrite and no waiting. */
    assert(telemetry_state.count == 2 && telemetry_state.dropped == 1);
    assert(telemetry_state.sequence == 3);
    telemetry_drain(); /* Backpressure: transport accepts zero bytes. */
    assert(telemetry_state.count == 2 && telemetry_state.offset == 0 && position == 0);
    accept_limit = 7; /* Exercise partial writes across frame boundaries. */
    for (unsigned i = 0; i < 100 && telemetry_state.count; ++i)
        telemetry_drain();
    assert(telemetry_state.count == 0 && telemetry_state.offset == 0);
    assert(position == 2 * TELEMETRY_FRAME_SIZE);
    telemetry_capture(400); /* Sequence gap and dropped count visible in next frame. */
    for (unsigned i = 0; i < 100 && telemetry_state.count; ++i)
        telemetry_drain();
    assert(position == 3 * TELEMETRY_FRAME_SIZE && telemetry_state.count == 0);
    if (argc == 2) {
        FILE *file = fopen(argv[1], "wb");
        assert(file);
        assert(fwrite(output, 1, position, file) == position);
        assert(fclose(file) == 0);
    }
    puts("PASS: telemetry backpressure, partial writes, bounded queue, snapshot copies");
    return 0;
}
