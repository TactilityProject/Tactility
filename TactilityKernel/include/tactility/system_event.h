// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tactility/error.h>
#include <tactility/freertos/freertos.h>
#include <tactility/freertos/semphr.h>
#include <tactility/freertos/task.h>

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
 * and polled with system_event_await(). Unlike system_event_callback_t, `event` is a by-value
 * copy that remains valid for the subscription's lifetime (until the next matching event
 * overwrites it), not just for the duration of a callback.
 * @warning Must be zero-initialized before the first system_event_subscribe() call (e.g.
 * `SystemEventSubscription sub = {};` in C++, `SystemEventSubscription sub = {0};` in C, or
 * static/global storage) - system_event_subscribe() reads `internal.unsubscribe_in_progress`
 * before it writes it, to detect reuse of a node still being torn down by a concurrent
 * system_event_unsubscribe() call; on indeterminate (non-zeroed) storage that read is undefined
 * behavior. Not required again for a later system_event_subscribe() reusing the same node after
 * system_event_unsubscribe() - the fields it depends on are fully owned/maintained by this API
 * from the first successful registration onward.
 */
struct SystemEventSubscription {
    /** `event.type` is the event type to subscribe to; set by the caller before
     * system_event_subscribe(). The rest of `event` (timestamp/data/data_len) is populated by
     * each matching system_event_emit() - see the @warning above. */
    struct SystemEvent event;

    /** Implementation-only bookkeeping; do not read or write directly. */
    struct {
        /** Own wakeup signal, not the subscribing task's shared default notification value - a
         * task with more than one poll subscription would otherwise have events for one
         * subscription wake (and consume the notification meant for) system_event_await()
         * calls on another. */
        SemaphoreHandle_t semaphore;

        uint32_t sequence;
        uint32_t consumed_sequence;

        /** Number of tasks currently blocked in system_event_await() on `semaphore` -
         * system_event_unsubscribe() waits for this to reach 0 before deleting it, since
         * FreeRTOS requires no task be blocked on a semaphore when it's deleted. */
        int waiter_count;
        /** Set by system_event_unsubscribe() before it gives `semaphore` and waits, so a task
         * already blocked in system_event_await() bails out (ERROR_INVALID_STATE) instead of
         * waiting out its full timeout. Reset once system_event_unsubscribe() finishes
         * draining old awaiters (see unsubscribe_in_progress) - not simply "on the next
         * system_event_subscribe()", so a fresh registration can never observe a stale `true`
         * left over from an unsubscribe that hasn't returned yet. */
        bool cancelled;
        /** True from the moment system_event_unsubscribe() unlinks `sub` until it has finished
         * draining old awaiters and deleted the old semaphore. system_event_subscribe() spins
         * until this clears before reusing `sub` - otherwise a new registration could reset
         * waiter_count/cancelled (both shared with the old registration, there being only one
         * `sub`) out from under the old system_event_unsubscribe() call still relying on them,
         * or hand out a new semaphore for that same call to then promptly delete instead of the
         * old one, while an old awaiter is still blocked on the real old semaphore. */
        bool unsubscribe_in_progress;

        struct SystemEventSubscription* next;
    } internal;
};

/**
 * Register a poll subscription for events of @a sub->type.
 * @warning Does not work in ISR context.
 * @warning On its very first call for a given @a sub, @a sub must have been zero-initialized -
 * see SystemEventSubscription's @warning.
 * @warning If @a sub was just passed to system_event_unsubscribe() (e.g. reusing a node for a
 * new registration) and that call hasn't returned yet on another task, this call blocks
 * (briefly - not for the full duration of anyone's timeout) until it does, before registering -
 * see SystemEventSubscription::internal.unsubscribe_in_progress.
 * @param[in,out] sub subscription to register; caller sets @a sub->type beforehand, owns the
 * storage, and must keep it alive (and stationary) until unsubscribed
 * @retval ERROR_NONE on success
 * @retval ERROR_OUT_OF_MEMORY failed to allocate the subscription's wakeup semaphore; @a sub
 * was not registered
 * @retval ERROR_INVALID_STATE @a sub is already registered
 */
error_t system_event_subscribe(struct SystemEventSubscription* sub);

/**
 * Remove a previously registered poll subscription.
 * @warning Does not work in ISR context.
 * @warning Blocks (briefly - not for the full duration of anyone's timeout) until any task
 * currently blocked in system_event_await() on @a sub has woken up and left, so it's safe to
 * delete the subscription's semaphore before this call returns. A blocked awaiter is woken
 * (with ERROR_INVALID_STATE) as part of this call rather than left to time out on its own.
 * A concurrent system_event_subscribe() reusing the same @a sub waits out this same window
 * (see system_event_subscribe()'s @warning) rather than racing it.
 * @param[in] sub subscription to remove, as passed to system_event_subscribe()
 * @return ERROR_NONE on success, ERROR_NOT_FOUND if no matching subscription exists
 */
error_t system_event_unsubscribe(struct SystemEventSubscription* sub);

/**
 * Blocks the calling task until a new event arrives for @a sub, or timeout elapses.
 * @warning Poll subscriptions coalesce to the latest event, they are not a queue: if
 * system_event_emit() is called more than once for @a sub->event.type between two
 * system_event_await() calls, only the most recent event's data/timestamp is visible via
 * system_event_get_data()/system_event_get_timestamp() afterward - intermediate events are
 * silently overwritten, never delivered. Use system_event_callback_add() instead if every
 * individual event matters.
 * @warning Cannot be called concurrently from different tasks. Each tasks must have its own subscription.
 * @param[in,out] sub subscription to wait on, as passed to system_event_subscribe()
 * @param[in] timeout max ticks to wait
 * @retval ERROR_NONE an event arrived
 * @retval ERROR_TIMEOUT @a timeout elapsed first
 * @retval ERROR_INVALID_STATE another task called system_event_unsubscribe() on @a sub while
 * this call was blocked
 */
error_t system_event_await(struct SystemEventSubscription* sub, TickType_t timeout);

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
