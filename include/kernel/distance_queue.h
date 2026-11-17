#ifndef KERNEL_DISTANCE_QUEUE_H
#define KERNEL_DISTANCE_QUEUE_H

#include <kernel/messages.h>

enum { DISTANCE_QUEUE_CAPACITY = 4 };

/* One producer and one consumer, both called sequentially in foreground code.
 * Not interrupt-safe or multicore-safe. No locks/atomics are needed under that
 * restriction. Each pop removes a value; this is not broadcast pub/sub yet. */
struct distance_queue {
    struct distance_sample slots[DISTANCE_QUEUE_CAPACITY];
    uint32_t head; /* Next occupied slot to read, when count != 0. */
    uint32_t tail; /* Next slot to write, when count != capacity. */
    uint32_t count;
    uint32_t high_water;
    uint64_t published;
    uint64_t consumed;
    uint64_t dropped;
};

extern struct distance_queue sensor_queue;
void distance_queue_init(struct distance_queue *queue);
int distance_queue_push(struct distance_queue *queue, const struct distance_sample *sample);
int distance_queue_pop(struct distance_queue *queue, struct distance_sample *sample);

#endif
