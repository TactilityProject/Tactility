// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "instance.h"

#include <stddef.h>
#include <stdint.h>

#include <tactility/concurrent/task_event_group.h>
#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Identifies the kind of app-lifecycle event delivered through app_event_poll(). */
enum AppEventType {
    APP_EVENT_RESULT, // struct AppResultEventData
    APP_EVENT_CLOSE,  // no data - terminate now, permanently
    APP_EVENT_SIGNAL, // struct AppSignalEventData, see app/signal.h
};

/** Data for APP_EVENT_RESULT. */
struct AppResultEventData {
    uint32_t launch_id;
    /** The child app instance's own AppMainFn/AppLoaderApi::run() return value. By convention:
     * 0 = Ok, 1 = Cancelled, 2 = Error. Apps that need to hand back more than this (e.g. picked
     * text, a path) write it to their own stdout instead, for the caller to read via an
     * AppStream bound to it. See e.g. tt::app::inputdialog::start(). */
    int32_t result;
};

/** Data for APP_EVENT_SIGNAL. */
struct AppSignalEventData {
    /** A POSIX signal number, e.g. SIGHUP */
    int sig;
};

struct AppEvent {
    enum AppEventType type;
    /** Stamped by app_event_emit(); any value passed in by the caller is ignored. */
    uint64_t timestamp;
    union {
        /** Valid only when type == APP_EVENT_RESULT. */
        struct AppResultEventData result;
        /** Valid only when type == APP_EVENT_SIGNAL. */
        struct AppSignalEventData signal;
    };
};

/**
 * Number of events that can be queued per subscription before app_event_emit() starts
 * returning ERROR_RESOURCE (dropping the newest event, preserving FIFO order of what's
 * already queued). Deliberately generous: app-module's scheduler is the only emitter and it
 * serializes app-lifecycle transitions, so a given app can't realistically receive events
 * faster than the scheduler produces them one at a time.
 */
#define APP_EVENT_QUEUE_CAPACITY 4

/**
 * Caller-owned subscription node. Unlike TactilityKernel's system_event poll subscription
 * (which coalesces to the latest value), this queues events by value (FIFO) since dropping an
 * APP_EVENT_RESULT would be unacceptable.
 * @warning Fields other than `bit` are for internal use only; do not read or write them
 * directly.
 */
struct AppEventSubscription {
    /** Set by app_event_subscribe()/app_event_subscribe_with_app_id(). Read-only for the
     * caller: OR it into a task_event_group_wait() mask (alongside other subscriptions sharing
     * the same `event_group`) to block on this subscription and other event sources with one
     * call. */
    uint32_t bit;

    struct {
        /** The app instance this subscription receives events for; set by
         * app_event_subscribe()/app_event_subscribe_with_app_id(). */
        AppInstanceId app_instance_id;

        /** Caller-owned, borrowed; set by app_event_subscribe(). */
        struct TaskEventGroup* event_group;

        struct AppEvent queue[APP_EVENT_QUEUE_CAPACITY];
        uint8_t head;
        uint8_t count;

        struct AppEventSubscription* next;
    } internal;
};

/**
 * Register a subscription for events addressed to the calling app's own instance (identified via
 * app_scheduler_current_app_id()).
 * @warning Does not work in ISR context. Must be called from the app's own task.
 * @param[in,out] sub subscription to register; owns the storage, must stay alive (and
 * stationary) until unsubscribed
 * @param[in] event_group caller-owned group to wait on; must outlive @a sub (i.e. be
 * destructed only after app_event_unsubscribe()). To block for an event, call
 * task_event_group_wait()/task_event_group_wait_any() on this group (OR sub->bit into the mask,
 * or use _wait_any() to include every subscription sharing it), then drain with app_event_poll().
 * @retval ERROR_NONE on success
 * @retval ERROR_RESOURCE @a event_group has no free bits left to claim; @a sub was not registered
 * @retval ERROR_INVALID_STATE @a sub is already registered
 */
error_t app_event_subscribe(struct AppEventSubscription* sub, struct TaskEventGroup* event_group);

/**
 * Same as app_event_subscribe(), but for a caller that isn't running on @a app_instance_id's own
 * task (e.g. test code simulating multiple distinct app instances from one thread). Production
 * app code should use app_event_subscribe() instead.
 * @warning Does not work in ISR context.
 * @param[in,out] sub subscription to register; owns the storage, must stay alive (and
 * stationary) until unsubscribed
 * @param[in] event_group caller-owned group to wait on; same contract as app_event_subscribe()
 * @param[in] app_instance_id the app instance this subscription receives events for
 * @retval ERROR_NONE on success
 * @retval ERROR_RESOURCE @a event_group has no free bits left to claim; @a sub was not registered
 * @retval ERROR_INVALID_STATE @a sub is already registered
 */
error_t app_event_subscribe_with_app_id(struct AppEventSubscription* sub, struct TaskEventGroup* event_group, AppInstanceId app_instance_id);

/**
 * Remove a previously registered subscription.
 * @warning Does not work in ISR context.
 * @return ERROR_NONE on success, ERROR_NOT_FOUND if no matching subscription exists
 */
error_t app_event_unsubscribe(struct AppEventSubscription* sub);

/**
 * Non-blocking: pop the next event for @a sub if one is already queued.
 * @warning Never blocks. To wait for an event, block in task_event_group_wait()/
 * task_event_group_wait_any() on @a sub's event group first (see app_event_subscribe()), then
 * drain with this in a loop.
 * @retval ERROR_NONE @a out_event was filled
 * @retval ERROR_TIMEOUT nothing queued right now
 */
error_t app_event_poll(struct AppEventSubscription* sub, struct AppEvent* out_event);

/**
 * Emits a close event to the specified app.
 * @param[in] instance_id
 * @return ERROR_NONE when event was successfully emitted
 */
error_t app_event_emit_close(AppInstanceId instance_id);

#ifdef __cplusplus
}
#endif
