#include <kernel/distance_queue.h>

struct distance_queue sensor_queue;

void distance_queue_init(struct distance_queue *queue)
{
    /* Empty slots have no meaning, so do not spend time clearing their bytes. */
    queue->head = queue->tail = queue->count = queue->high_water = 0;
    queue->published = queue->consumed = queue->dropped = 0;
}

int distance_queue_push(struct distance_queue *queue, const struct distance_sample *sample)
{
    if (queue->count == DISTANCE_QUEUE_CAPACITY) {
        ++queue->dropped; /* Reject newest. Existing queued values stay intact. */
        return 0;
    }
    queue->slots[queue->tail] = *sample; /* Copy the value, not its pointer. */
    queue->tail = (queue->tail + 1) % DISTANCE_QUEUE_CAPACITY;
    ++queue->count;
    ++queue->published;
    if (queue->count > queue->high_water)
        queue->high_water = queue->count;
    return 1;
}

int distance_queue_pop(struct distance_queue *queue, struct distance_sample *sample)
{
    if (!queue->count)
        return 0; /* No waiting. Leave the caller's output unchanged. */
    *sample = queue->slots[queue->head];
    queue->head = (queue->head + 1) % DISTANCE_QUEUE_CAPACITY;
    --queue->count;
    ++queue->consumed;
    return 1;
}
