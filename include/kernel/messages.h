#ifndef KERNEL_MESSAGES_H
#define KERNEL_MESSAGES_H

#include <stdint.h>

/* Shared in-memory meaning, independent of sensor/controller implementations.
 * This C layout is NOT a portable wire format; telemetry will serialize fields. */
struct distance_sample {
    uint64_t sampled_at_ms;
    uint32_t sequence;
    uint32_t distance_mm;
    uint32_t valid;
};

#endif
