#include <kernel/telemetry.h>
#include <kernel/platform.h>
#include <kernel/sensor.h>
#include <kernel/controller.h>
#include <kernel/distance_queue.h>

struct periodic_task telemetry_task;
struct telemetry_state telemetry_state;

static void put16(unsigned char **next, uint16_t value)
{
    for (unsigned i = 0; i < 2; ++i) {
        *(*next)++ = (unsigned char)value;
        value >>= 8;
    }
}

static void put32(unsigned char **next, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) {
        *(*next)++ = (unsigned char)value;
        value >>= 8;
    }
}

static void put64(unsigned char **next, uint64_t value)
{
    for (unsigned i = 0; i < 8; ++i) {
        *(*next)++ = (unsigned char)value;
        value >>= 8;
    }
}

static uint16_t crc16(const unsigned char *bytes, unsigned length)
{
    /* CRC-16/CCITT-FALSE: poly 0x1021, init 0xffff, no reflection/xor-out.
     * Detects accidental corruption; this is not authentication. */
    uint16_t crc = 0xffff;
    for (unsigned i = 0; i < length; ++i) {
        crc ^= (uint16_t)bytes[i] << 8;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
    }
    return crc;
}

void telemetry_init(uint64_t now_ms)
{
    telemetry_state.head = telemetry_state.tail = telemetry_state.count = 0;
    telemetry_state.offset = telemetry_state.sequence = 0;
    telemetry_state.dropped = 0;
    telemetry_task = (struct periodic_task) {
        .period_ms = 100, .last_release_ms = now_ms, .run = telemetry_capture,
    };
}

void telemetry_capture(uint64_t now_ms)
{
    ++telemetry_state.sequence; /* Count attempts so rejected frames leave gaps. */
    if (telemetry_state.count == TELEMETRY_CAPACITY) {
        ++telemetry_state.dropped;
        return; /* Observation must never wait for a slow or absent reader. */
    }
    unsigned char *frame = telemetry_state.frames[telemetry_state.tail];
    unsigned char *next = frame;
    *next++ = 0xa5;
    *next++ = 0x5a;
    *next++ = 1; /* Protocol version. */
    *next++ = 1; /* Record kind: foreground state snapshot. */
    put16(&next, 104); /* Payload length, excluding header and CRC. */
    put32(&next, telemetry_state.sequence);
    put64(&next, now_ms);

    /* Explicit field encoding: no native struct bytes, padding, or pointers.
     * Foreground tasks execute sequentially; ISR never modifies these fields. */
    put32(&next, sensor_reading.sequence);
    put32(&next, sensor_reading.distance_mm);
    put32(&next, sensor_reading.valid);
    put64(&next, sensor_reading.sampled_at_ms);
    put32(&next, controller_state.last_sample.sequence);
    put64(&next, controller_task.last_start_ms);
    put64(&next, controller_state.sample_age_ms);
    put32(&next, controller_state.target_speed_mm_s);
    put32(&next, (uint32_t)controller_state.reason);
    put32(&next, sensor_queue.count);
    put32(&next, sensor_queue.high_water);
    put64(&next, sensor_queue.published);
    put64(&next, sensor_queue.consumed);
    put64(&next, sensor_queue.dropped);
    put64(&next, sensor_task.missed_releases);
    put64(&next, controller_task.missed_releases);
    put64(&next, telemetry_state.dropped);
    /* Header is 18 bytes; CRC covers version through the end of payload. */
    uint16_t checksum = crc16(frame + 2, (unsigned)(next - frame) - 2);
    put16(&next, checksum);
    telemetry_state.tail = (telemetry_state.tail + 1) % TELEMETRY_CAPACITY;
    ++telemetry_state.count;
}

void telemetry_drain(void)
{
    if (!telemetry_state.count)
        return;
    /* At most one FIFO-sized burst per main-loop iteration. No busy waiting.
     * A partial frame stays queued; snapshots are never interleaved. */
    unsigned remaining = TELEMETRY_FRAME_SIZE - telemetry_state.offset;
    unsigned written = platform_telemetry_write(
        telemetry_state.frames[telemetry_state.head] + telemetry_state.offset,
        remaining);
    telemetry_state.offset += written;
    if (telemetry_state.offset == TELEMETRY_FRAME_SIZE) {
        telemetry_state.offset = 0;
        telemetry_state.head = (telemetry_state.head + 1) % TELEMETRY_CAPACITY;
        --telemetry_state.count;
    }
}
