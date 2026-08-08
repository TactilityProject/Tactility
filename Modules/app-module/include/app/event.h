// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <tactility/error.h>
#include <tactility/freertos/freertos.h>
#include <tactility/freertos/task.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Identifies the kind of app-lifecycle event delivered through app_event_await(). */
enum AppEventType {
    APP_EVENT_RESULT, // struct AppResultEventData
    APP_EVENT_CLOSE,  // no data - terminate now, permanently
};

/** Data for APP_EVENT_RESULT. */
struct AppResultEventData {
    uint32_t launch_id;
    /** The child app instance's own AppMainFn/AppLoaderApi::run() return value. By convention:
     * 0 = Ok, 1 = Cancelled, 2 = Error. Apps that need to hand back more than this (e.g. picked
     * text, a path) expose their own "get last result" getter instead - see e.g.
     * tt::app::inputdialog::getLastText(). */
    int32_t result;
};

struct AppEvent {
    enum AppEventType type;
    /** Stamped by app_event_emit(); any value passed in by the caller is ignored. */
    uint64_t timestamp;
    /** Valid only when type == APP_EVENT_RESULT. */
    struct AppResultEventData result;
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
 * @warning Fields other than `app_instance_id` are for internal use only; do not read or write
 * them directly.
 */
struct AppEventSubscription {
    /** The app instance this subscription receives events for; set by the caller before app_event_subscribe(). */
    uint32_t app_instance_id;

    TaskHandle_t task;

    struct AppEvent queue[APP_EVENT_QUEUE_CAPACITY];
    uint8_t head;
    uint8_t count;

    struct AppEventSubscription* next;
};

/**
 * Register a subscription for events addressed to @a sub->app_instance_id.
 * @warning Does not work in ISR context.
 * @param[in,out] sub subscription to register; caller sets @a sub->app_instance_id beforehand,
 * owns the storage, and must keep it alive (and stationary) until unsubscribed
 * @return ERROR_NONE on success
 */
error_t app_event_subscribe(struct AppEventSubscription* sub);

/**
 * Remove a previously registered subscription.
 * @warning Does not work in ISR context.
 * @return ERROR_NONE on success, ERROR_NOT_FOUND if no matching subscription exists
 */
error_t app_event_unsubscribe(struct AppEventSubscription* sub);

/**
 * Deliver @a event to every subscription registered for @a app_instance_id (normally exactly one).
 * @warning Does not work in ISR context.
 * @retval ERROR_NONE delivered to at least one subscription
 * @retval ERROR_NOT_FOUND no subscription is registered for @a app_instance_id
 * @retval ERROR_RESOURCE at least one matching subscription's queue was full; the event was
 * dropped for that subscription (still delivered to any other matching subscription)
 */
error_t app_event_emit(uint32_t app_instance_id, const struct AppEvent* event);

/**
 * Pop the next event for @a sub, blocking up to @a timeout if the queue is currently empty.
 * @retval ERROR_NONE @a out_event was filled
 * @retval ERROR_TIMEOUT no event arrived before the timeout elapsed
 */
error_t app_event_await(struct AppEventSubscription* sub, struct AppEvent* out_event, TickType_t timeout);

#ifdef __cplusplus
}
#endif
