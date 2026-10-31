// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tactility/concurrent/task_event_group.h>
#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Device;
struct FileSystem;

/** Identifies a system-wide event */
enum SystemEventType {
    KERNEL_EVENT_BOOT_COMPLETED, // No data
    KERNEL_EVENT_NETWORK_CONNECTED, // struct NetworkConnectedEvent
    KERNEL_EVENT_NETWORK_DISCONNECTED, // struct NetworkDisconnectedEvent
    KERNEL_EVENT_FILE_SYSTEM_MOUNTED, // struct FileSystemMountedEvent
    KERNEL_EVENT_FILE_SYSTEM_UNMOUNTED, // struct FileSystemUnmountedEvent
    KERNEL_EVENT_SERVICE_STARTED, // ServiceStartedEvent
    KERNEL_EVENT_SERVICE_STOPPED, // ServiceStoppedEvent
    KERNEL_EVENT_TIME_CHANGED, // No data - fired whenever system time is set (NTP sync, RTC restore, manual change)
    KERNEL_EVENT_DEEP_SLEEP_PREPARE, // No data - fired synchronously right before the system enters deep sleep
    KERNEL_EVENT_LOCATION_TIME, // struct LocationTimeEvent
};

/** Data for KERNEL_EVENT_NETWORK_CONNECTED. */
struct NetworkConnectedEvent {
    struct Device* device;
    uint32_t ipv4_addr;
    uint32_t gateway;
};

/** Data for KERNEL_EVENT_NETWORK_DISCONNECTED. */
struct NetworkDisconnectedEvent {
    struct Device* device;
};

/** Data for KERNEL_EVENT_FILE_SYSTEM_MOUNTED. */
struct FileSystemMountedEvent {
    struct FileSystem* file_system;
};

/** Data for KERNEL_EVENT_FILE_SYSTEM_UNMOUNTED. */
struct FileSystemUnmountedEvent {
    struct FileSystem* file_system;
};

/** Data for KERNEL_EVENT_SERVICE_STARTED. */
struct ServiceStartedEvent {
    const char* id;
};

/** Data for KERNEL_EVENT_SERVICE_STOPPED. */
struct ServiceStoppedEvent {
    const char* id;
};

/** Data for KERNEL_EVENT_LOCATION_TIME. Fired at most once per minute while a satellite provider reports fixes. */
struct LocationTimeEvent {
    /** Current time according to the provider, in milliseconds since the Unix epoch */
    int64_t time_ms;
};

/** Size of the largest type-specific event struct documented in SystemEventType, i.e. the
 * embedded buffer size needed by SystemEvent/SystemEventSubscription to hold any event's
 * payload by value. */
#define SYSTEM_EVENT_MAX_DATA_SIZE (sizeof(struct NetworkConnectedEvent))

/**
 * A system-wide event as delivered to a system_event_callback_t.
 * `data` (up to `data_len` bytes, `SYSTEM_EVENT_MAX_DATA_SIZE` max) is a by-value copy of the
 * type-specific struct documented next to `type`'s enum value in SystemEventType (or unused,
 * with `data_len` 0, when none is documented) - like SystemEventSubscription's `data`, but only
 * valid for the duration of the callback rather than for the subscription's lifetime.
 */
struct SystemEvent {
    enum SystemEventType type;
    /** Microseconds since boot, from get_micros_since_boot(). */
    uint64_t timestamp;
    uint8_t data[SYSTEM_EVENT_MAX_DATA_SIZE];
    size_t data_len;
};

/**
 * @param[in] event the event being delivered; only valid for the duration of the call
 * @param[in] context the context pointer passed to system_event_callback_add()
 */
typedef void (*system_event_callback_t)(struct SystemEvent* event, void* context);

/**
 * Subscribe to system events of a given type.
 * @warning Does not work in ISR context.
 * @warning @a callback is invoked synchronously, on the caller's task, from within
 * system_event_emit(). The internal subscription lock is not held during the call, so
 * @a callback may itself call system_event_callback_add(), system_event_callback_remove() or
 * system_event_emit() without deadlocking - but a subscribe/unsubscribe made from within
 * a callback only takes effect for events emitted after the current system_event_emit()
 * call returns, since that call already snapshotted the subscriptions it will invoke.
 * @param[in] type the event type to subscribe to
 * @param[in] callback the callback to invoke when a matching event is emitted
 * @param[in] context an opaque pointer passed back to @a callback unmodified
 * @return ERROR_NONE on success
 */
error_t system_event_callback_add(
    enum SystemEventType type,
    system_event_callback_t callback,
    void *context
);

/**
 * Remove a previously added subscription.
 * @warning Does not work in ISR context.
 * @param[in] type the event type passed to the matching system_event_callback_add() call
 * @param[in] callback the callback passed to the matching system_event_callback_add() call
 * @return ERROR_NONE on success, ERROR_NOT_FOUND if no matching subscription exists
 */
error_t system_event_callback_remove(
    enum SystemEventType type,
    system_event_callback_t callback
);

/**
 * Emit a system event, synchronously invoking every subscription registered for @a type
 * (in subscription order) on the calling task before returning.
 * @warning Does not work in ISR context.
 * @param[in] type the event type
 * @param[in] data optional pointer to the type-specific event struct (see SystemEventType);
 * only valid for the duration of this call, subscribers must not retain it
 * @param[in] data_len size of @a data in bytes (0 if @a data is NULL)
 * @return ERROR_NONE on success
 */
error_t system_event_emit(
    enum SystemEventType type,
    const void* data,
    size_t data_len
);

/** Size of the largest type-specific event struct documented in SystemEventType, i.e. the
 * embedded buffer size needed by SystemEventSubscription to hold any event's payload by value. */
#define SYSTEM_EVENT_MAX_DATA_SIZE (sizeof(struct NetworkConnectedEvent))

/**
 * Poll subscription: caller-owned node, registered with system_event_subscribe()
 * and polled with system_event_poll(). Unlike system_event_callback_t, `event` is a by-value
 * copy that remains valid for the subscription's lifetime (until the next matching event
 * overwrites it), not just for the duration of a callback.
 */
struct SystemEventSubscription {
    /** `event.type` is the event type to subscribe to; set by the caller before
     * system_event_subscribe(). The rest of `event` (timestamp/data/data_len) is populated by
     * each matching system_event_emit() - see the @warning above. */
    struct SystemEvent event;

    /** Set by system_event_subscribe(). Read-only for the caller: OR it into a
     * task_event_group_wait() mask (alongside other subscriptions sharing the same
     * `internal.event_group`) to block on this subscription and other event sources with one
     * call. */
    uint32_t bit;

    /** Implementation-only bookkeeping; do not read or write directly. */
    struct {
        /** Caller-owned, borrowed; set by system_event_subscribe(). A task with more than one
         * poll subscription shares one group across them - each subscription claims its own
         * bit, so an event for one can't wake (and consume the signal meant for) another. */
        struct TaskEventGroup* event_group;

        uint32_t sequence;
        uint32_t consumed_sequence;

        /** Set by system_event_unsubscribe(). Diagnostic only: lets a subsequent
         * system_event_poll() call report ERROR_INVALID_STATE instead of ERROR_TIMEOUT. Not a
         * safety mechanism - see system_event_unsubscribe()'s @warning. */
        bool cancelled;

        struct SystemEventSubscription* next;
    } internal;
};

/**
 * Register a poll subscription for events of @a sub->type.
 * @warning Does not work in ISR context.
 * @param[in,out] sub subscription to register; caller sets @a sub->type beforehand, owns the
 * storage, and must keep it alive (and stationary) until unsubscribed
 * @param[in] event_group caller-owned group to wait on; must outlive @a sub (i.e. be
 * destructed only after system_event_unsubscribe()). To block for an event, call
 * task_event_group_wait()/task_event_group_wait_any() on this group (OR sub->bit into the mask,
 * or use _wait_any() to include every subscription sharing it), then poll with
 * system_event_poll().
 * @retval ERROR_NONE on success
 * @retval ERROR_RESOURCE @a event_group has no free bits left to claim; @a sub was not registered
 * @retval ERROR_INVALID_STATE @a sub is already registered
 */
error_t system_event_subscribe(struct SystemEventSubscription* sub, struct TaskEventGroup* event_group);

/**
 * Remove a previously registered poll subscription.
 * @warning Does not work in ISR context.
 * @warning Does not wait for a task concurrently blocked in task_event_group_wait()/
 * task_event_group_wait_any() on @a sub's bit to leave before releasing that bit - the caller
 * must ensure no other task is still waiting on @a sub before unsubscribing it. (A blocked task
 * is still nudged awake as a best-effort courtesy - its subsequent system_event_poll() call will
 * report ERROR_INVALID_STATE - but this is diagnostic, not a guarantee.)
 * @param[in] sub subscription to remove, as passed to system_event_subscribe()
 * @return ERROR_NONE on success, ERROR_NOT_FOUND if no matching subscription exists
 */
error_t system_event_unsubscribe(struct SystemEventSubscription* sub);

/**
 * Non-blocking: check whether a new event has arrived for @a sub since the last call.
 * @warning Never blocks. To wait, block in task_event_group_wait()/task_event_group_wait_any()
 * on @a sub's bit first (see system_event_subscribe()), then call this.
 * @warning Poll subscriptions coalesce to the latest event, they are not a queue: if
 * system_event_emit() is called more than once for @a sub->event.type between two
 * system_event_poll() calls, only the most recent event's data/timestamp is visible via
 * system_event_get_data()/system_event_get_timestamp() afterward - intermediate events are
 * silently overwritten, never delivered. Use system_event_callback_add() instead if every
 * individual event matters.
 * @warning Cannot be called concurrently from different tasks. Each task must have its own subscription.
 * @param[in,out] sub subscription to poll, as passed to system_event_subscribe()
 * @retval ERROR_NONE a new event arrived - read it via @a sub->event or system_event_get_data()
 * @retval ERROR_TIMEOUT no new event since the last call
 * @retval ERROR_INVALID_STATE @a sub was unsubscribed (best-effort diagnostic, not guaranteed -
 * see system_event_unsubscribe()'s @warning)
 */
error_t system_event_poll(struct SystemEventSubscription* sub);

/**
 * Copies @a sub's current event payload (the data from the most recent system_event_emit()
 * that reached it) into @a data.
 * @param[in] sub subscription to read the payload from, as passed to system_event_subscribe()
 * @param[out] data buffer to copy the payload into
 * @param[in] data_len size of @a data
 * @retval ERROR_NONE on success
 * @retval ERROR_BUFFER_OVERFLOW @a data_len is smaller than the stored payload - @a data is
 * left untouched
 */
error_t system_event_get_data(struct SystemEventSubscription* sub, uint8_t* data, size_t data_len);

#ifdef __cplusplus
}
#endif
