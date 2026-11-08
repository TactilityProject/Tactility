// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <tactility/concurrent/task_event_group.h>
#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A function that runs on the file operations task.
 * It may block on file and flash access, which tasks and callbacks with a stack in external RAM or the LVGL task must not do.
 * Results are passed back through the context, which must stay valid until the function has completed.
 */
typedef void (*file_ops_work_fn_t)(void* context);

/**
 * Caller-owned subscription that reports when queued work has completed.
 * Same subscribe/await/poll shape as http_download and app events: the subscription claims a bit of a
 * TaskEventGroup that is signalled whenever work queued with this subscription completes.
 * @warning Fields other than `bit` are for internal use only.
 */
struct FileOpsSubscription {
    /** Set by file_ops_subscribe(). Read-only for the caller: OR it into a task_event_group_wait() mask
     * to wait for completion together with other event sources. */
    uint32_t bit;

    struct {
        /** Caller-owned, borrowed. Set by file_ops_subscribe(). */
        struct TaskEventGroup* event_group;
        struct FileOpsLink* link;
    } internal;
};

/**
 * Registers a subscription.
 * @warning Does not work in ISR context.
 * @param[in,out] sub the subscription, which the caller owns and must keep alive and stationary until file_ops_unsubscribe()
 * @param[in] event_group caller-owned group that must outlive @a sub
 * @retval ERROR_NONE on success
 * @retval ERROR_RESOURCE @a event_group has no free bits left to claim
 * @retval ERROR_OUT_OF_MEMORY the subscription's internal state could not be allocated
 */
error_t file_ops_subscribe(struct FileOpsSubscription* sub, struct TaskEventGroup* event_group);

/**
 * Removes a subscription. Safe to call at any time, including while work queued with it hasn't run yet: the
 * work still runs, but the subscription and its event group are never touched again once this returns.
 * @warning Does not work in ISR context.
 * @retval ERROR_NONE on success
 * @retval ERROR_NOT_FOUND @a sub is not subscribed
 */
error_t file_ops_unsubscribe(struct FileOpsSubscription* sub);

/**
 * Queues work to run on the file operations task, in the order that it was queued.
 * The task is created when work is queued and no task is running, and it ends when the queue is empty.
 * @param[in] work the function to run
 * @param[in] context passed to @a work, must stay valid until the work has completed
 * @param[in] sub a subscription that is notified when the work completed, or NULL
 * @retval ERROR_NONE the work was queued
 * @retval ERROR_INVALID_ARGUMENT @a sub is not subscribed
 * @retval ERROR_OUT_OF_MEMORY the queue entry could not be allocated
 * @retval ERROR_RESOURCE the task could not be started
 */
error_t file_ops_queue(file_ops_work_fn_t work, void* context, struct FileOpsSubscription* sub);

/**
 * Tells whether the caller is running on the file operations task.
 * Code that waits for queued work must run that work directly when this is true, as the task can't wait for itself.
 */
bool file_ops_is_worker(void);

/**
 * Non-blocking: consumes one completion of work that was queued with @a sub.
 * @retval ERROR_NONE a completion was consumed
 * @retval ERROR_TIMEOUT no queued work has completed
 */
error_t file_ops_poll(struct FileOpsSubscription* sub);

/**
 * Waits until work that was queued with @a sub has completed, and consumes one completion.
 * Returns immediately when a completion is already available.
 * @param[in] timeout the maximum time to wait
 * @retval ERROR_NONE a completion was consumed
 * @retval ERROR_TIMEOUT no queued work completed within @a timeout
 */
error_t file_ops_await(struct FileOpsSubscription* sub, TickType_t timeout);

#ifdef __cplusplus
}
#endif
