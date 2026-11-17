#include <kernel/periodic.h>

void periodic_dispatch(struct periodic_task *task, uint64_t now_ms)
{
    if (!task->period_ms || !task->run)
        return;

    /* Unsigned subtraction also handles the uint64_t clock rolling over,
     * provided we check the task at least once per full clock cycle. */
    uint64_t elapsed = now_ms - task->last_release_ms;
    if (elapsed < task->period_ms)
        return;

    uint64_t releases = elapsed / task->period_ms;
    /* Preserve the original phase. Skip old releases rather than replaying
     * an unbounded burst of callbacks when the foreground was delayed. */
    task->last_release_ms += releases * task->period_ms;
    task->missed_releases += releases - 1;
    task->last_start_ms = now_ms;
    task->last_lateness_ms = now_ms - task->last_release_ms;
    ++task->dispatch_count;
    task->run(now_ms);
}
