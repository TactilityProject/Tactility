#include <tactility/system_event.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/delay.h>
#include <tactility/error.h>
#include <tactility/time.h>

#include <algorithm>
#include <cstring>
#include <new>
#include <vector>

struct KernelEventSubscription {
    SystemEventType type;
    system_event_callback_t callback;
    void* callback_context;
};

static std::vector<KernelEventSubscription> subscriptions;

// Mutex is constructed/destructed via a static-lifetime wrapper because struct Mutex
// itself has no constructor: mutex_lock() on an unconstructed handle is undefined
// behaviour (the raw QueueHandle_t would be null).
struct KernelEventMutex {
    Mutex handle {};
    KernelEventMutex() { mutex_construct(&handle); }
    ~KernelEventMutex() { mutex_destruct(&handle); }
};

static KernelEventMutex subscriptions_mutex;

// Intrusive singly-linked list of poll subscriptions (system_event_subscribe()/_unsubscribe()/
// _await()), separate from the callback-based `subscriptions` vector above. Guarded by its own
// mutex since notifying a poll subscriber never invokes caller code (just a memcpy and an
// xTaskNotifyGive), so there is no reentrancy concern requiring a snapshot-then-unlock dance.
static SystemEventSubscription* poll_subscriptions = nullptr;
static KernelEventMutex poll_subscriptions_mutex;

extern "C" {

error_t system_event_callback_add(
    SystemEventType type,
    system_event_callback_t callback,
    void* context
) {
    mutex_lock(&subscriptions_mutex.handle);
    subscriptions.push_back(KernelEventSubscription { type, callback, context });
    mutex_unlock(&subscriptions_mutex.handle);

    return ERROR_NONE;
}

error_t system_event_callback_remove(
    SystemEventType type,
    system_event_callback_t callback
) {
    mutex_lock(&subscriptions_mutex.handle);
    const auto iterator = std::ranges::find_if(subscriptions, [type, callback](const KernelEventSubscription& subscription) {
        return subscription.type == type && subscription.callback == callback;
    });
    error_t result = ERROR_NOT_FOUND;
    if (iterator != subscriptions.end()) {
        subscriptions.erase(iterator);
        result = ERROR_NONE;
    }
    mutex_unlock(&subscriptions_mutex.handle);

    return result;
}

// Copies `data` into every current poll subscriber of `type` and signals its wakeup semaphore.
// Held entirely under the lock: unlike the callback path, this never invokes caller code
// (just a memcpy and a semaphore give), so there is nothing that could reenter and deadlock.
static void notify_poll_subscribers(
    SystemEventType type,
    uint64_t timestamp,
    const void* data,
    size_t data_len
) {
    mutex_lock(&poll_subscriptions_mutex.handle);

    for (SystemEventSubscription* sub = poll_subscriptions; sub != nullptr; sub = sub->internal.next) {
        if (sub->event.type == type) {
            sub->event.timestamp = timestamp;
            const size_t copied_len = std::min(data_len, SYSTEM_EVENT_MAX_DATA_SIZE);
            if (copied_len > 0) {
                std::memcpy(sub->event.data, data, copied_len);
            }
            sub->event.data_len = copied_len;
            sub->internal.sequence++;
            xSemaphoreGive(sub->internal.semaphore);
        }
    }

    mutex_unlock(&poll_subscriptions_mutex.handle);
}

static error_t notify_listeners(
    SystemEvent& event
) {
    // Snapshot matching subscriptions under the lock, then invoke after unlocking: a
    // callback calling system_event_callback_add(), system_event_callback_remove() or
    // system_event_emit() would otherwise deadlock against this same (non-recursive)
    // mutex, and a slow callback would block every other thread's subscribe/unsubscribe
    // for the duration of this emit.
    //
    // Nothing between mutex_lock() and mutex_unlock() below may throw.
    // Count first, then use new(std::nothrow) to allocate the exact size and report
    // failure through the return value instead; the fill loop below is then a plain
    // assignment of a trivially-copyable struct, which cannot throw or reallocate.

    mutex_lock(&subscriptions_mutex.handle);

    size_t match_count = 0;
    for (const auto& subscription : subscriptions) {
        if (subscription.type == event.type) {
            match_count++;
        }
    }

    KernelEventSubscription* matching = (match_count > 0)
        ? new (std::nothrow) KernelEventSubscription[match_count]
        : nullptr;
    if (match_count > 0 && matching == nullptr) {
        mutex_unlock(&subscriptions_mutex.handle);
        return ERROR_OUT_OF_MEMORY;
    }

    size_t matched_count = 0;
    for (const auto& subscription : subscriptions) {
        if (subscription.type == event.type) {
            matching[matched_count++] = subscription;
        }
    }

    mutex_unlock(&subscriptions_mutex.handle);

    for (size_t i = 0; i < matched_count; i++) {
        matching[i].callback(&event, matching[i].callback_context);
    }

    delete[] matching;

    return ERROR_NONE;
}

error_t system_event_emit(
    SystemEventType type,
    const void* data,
    size_t data_len
) {
    SystemEvent event {};
    event.type = type;
    event.timestamp = get_micros_since_boot();
    const size_t copied_len = std::min(data_len, SYSTEM_EVENT_MAX_DATA_SIZE);
    if (copied_len > 0) {
        std::memcpy(event.data, data, copied_len);
    }
    event.data_len = copied_len;

    notify_poll_subscribers(type, event.timestamp, data, data_len);
    auto error = notify_listeners(event);
    if (error != ERROR_NONE) { return error; }

    return ERROR_NONE;
}

error_t system_event_subscribe(SystemEventSubscription* sub) {
    // Wait out any system_event_unsubscribe() call still draining old awaiters for this same
    // `sub` on another task (see internal.unsubscribe_in_progress). waiter_count/cancelled
    // belong to `sub` itself, not to a given registration - reusing `sub` before that call
    // finishes would reset them out from under it, and could hand out a fresh semaphore for it
    // to then promptly delete instead of the old one, while an old awaiter is still blocked on
    // the real old semaphore.
    while (true) {
        mutex_lock(&poll_subscriptions_mutex.handle);
        bool busy = sub->internal.unsubscribe_in_progress;
        mutex_unlock(&poll_subscriptions_mutex.handle);

        if (!busy) {
            break;
        }
        delay_ticks(pdMS_TO_TICKS(10));
    }

    SemaphoreHandle_t semaphore = xSemaphoreCreateBinary();
    if (semaphore == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    mutex_lock(&poll_subscriptions_mutex.handle);

    // Check-and-insert in one critical section: registering the same `sub` twice would link
    // it into a list that already contains it, creating a cycle that notify_poll_subscribers()
    // would then traverse forever while holding this same mutex.
    for (SystemEventSubscription* existing = poll_subscriptions; existing != nullptr; existing = existing->internal.next) {
        if (existing == sub) {
            mutex_unlock(&poll_subscriptions_mutex.handle);
            vSemaphoreDelete(semaphore);
            return ERROR_INVALID_STATE;
        }
    }

    sub->internal.semaphore = semaphore;
    sub->internal.sequence = 0;
    sub->internal.consumed_sequence = 0;
    sub->internal.waiter_count = 0;
    sub->internal.cancelled = false;
    sub->event.data_len = 0;
    sub->internal.next = poll_subscriptions;
    poll_subscriptions = sub;

    mutex_unlock(&poll_subscriptions_mutex.handle);

    return ERROR_NONE;
}

error_t system_event_unsubscribe(SystemEventSubscription* sub) {
    error_t result = ERROR_NOT_FOUND;
    SemaphoreHandle_t semaphore_to_delete = nullptr;

    mutex_lock(&poll_subscriptions_mutex.handle);
    for (SystemEventSubscription** link = &poll_subscriptions; *link != nullptr; link = &(*link)->internal.next) {
        if (*link == sub) {
            *link = sub->internal.next;
            result = ERROR_NONE;
            break;
        }
    }
    if (result == ERROR_NONE) {
        // Unlinked first, so notify_poll_subscribers() can no longer reach this subscription.
        // Mark it cancelled (checked by system_event_await()'s loop) and capture the semaphore
        // handle into a local variable rather than deleting it via sub->internal.semaphore
        // directly - a concurrent system_event_subscribe() re-registering this same `sub` after
        // this point would overwrite that field with a freshly created semaphore, and we must
        // not delete the wrong (newly active) one.
        sub->internal.cancelled = true;
        // Blocks a concurrent system_event_subscribe() from reusing `sub` until this whole
        // call returns - see internal.unsubscribe_in_progress and system_event_subscribe().
        sub->internal.unsubscribe_in_progress = true;
        semaphore_to_delete = sub->internal.semaphore;
        sub->internal.semaphore = nullptr;
    }
    mutex_unlock(&poll_subscriptions_mutex.handle);

    if (result != ERROR_NONE) {
        return result;
    }

    // Nudge any task already blocked in system_event_await() (it captured its own local copy
    // of this same semaphore handle before this point, so it's unaffected by the field having
    // just been cleared above) so it re-checks `cancelled` and bails out now instead of waiting
    // out its full timeout, then wait for it to actually leave the semaphore before deleting it
    // - FreeRTOS requires no task be blocked on a semaphore when it's deleted.
    xSemaphoreGive(semaphore_to_delete);
    while (true) {
        mutex_lock(&poll_subscriptions_mutex.handle);
        bool still_waiting = sub->internal.waiter_count > 0;
        mutex_unlock(&poll_subscriptions_mutex.handle);

        if (!still_waiting) {
            break;
        }
        delay_ticks(pdMS_TO_TICKS(10));
    }

    vSemaphoreDelete(semaphore_to_delete);

    // Reset under the lock, together, as the last step - only past this point is `sub` safe
    // for system_event_subscribe() to reuse (see internal.unsubscribe_in_progress and the
    // busy-wait at the top of system_event_subscribe()).
    mutex_lock(&poll_subscriptions_mutex.handle);
    sub->internal.cancelled = false;
    sub->internal.unsubscribe_in_progress = false;
    mutex_unlock(&poll_subscriptions_mutex.handle);

    return ERROR_NONE;
}

error_t system_event_await(SystemEventSubscription* sub, TickType_t timeout) {
    mutex_lock(&poll_subscriptions_mutex.handle);
    SemaphoreHandle_t semaphore = sub->internal.semaphore;
    sub->internal.waiter_count++;
    mutex_unlock(&poll_subscriptions_mutex.handle);

    error_t result = ERROR_NONE;

    // sequence/consumed_sequence are written by notify_poll_subscribers() under
    // poll_subscriptions_mutex - read (and, on a match, updated) under the same lock each
    // iteration, rather than compared lock-free, so a concurrent emit can't land between an
    // unlocked read and this loop acting on it.
    //
    // Compare against consumed_sequence, not a sequence snapshot taken now - an emit that
    // landed between system_event_subscribe() and this call already incremented sequence and
    // gave the semaphore, so that event is pending but unconsumed. Snapshotting "now" would
    // make the loop wait for yet another event instead of returning this already-pending one.
    while (true) {
        mutex_lock(&poll_subscriptions_mutex.handle);
        bool pending = sub->internal.sequence != sub->internal.consumed_sequence;
        bool cancelled = sub->internal.cancelled;
        if (pending) {
            sub->internal.consumed_sequence = sub->internal.sequence;
        }
        mutex_unlock(&poll_subscriptions_mutex.handle);

        if (pending) {
            break;
        }
        if (cancelled) {
            result = ERROR_INVALID_STATE;
            break;
        }
        if (xSemaphoreTake(semaphore, timeout) == pdFALSE) {
            result = ERROR_TIMEOUT;
            break;
        }
    }

    mutex_lock(&poll_subscriptions_mutex.handle);
    sub->internal.waiter_count--;
    mutex_unlock(&poll_subscriptions_mutex.handle);

    return result;
}

error_t system_event_get_data(SystemEventSubscription* sub, uint8_t* data, size_t data_len) {
    // sub->event.* is written by notify_poll_subscribers() under poll_subscriptions_mutex -
    // the length check and the copy must happen as one snapshot under the same lock, otherwise
    // a concurrent emit could grow data_len (or overwrite data) between the check and the
    // memcpy below.
    mutex_lock(&poll_subscriptions_mutex.handle);
    error_t result = ERROR_NONE;
    if (data_len < sub->event.data_len) {
        result = ERROR_BUFFER_OVERFLOW;
    } else {
        std::memcpy(data, sub->event.data, sub->event.data_len);
    }
    mutex_unlock(&poll_subscriptions_mutex.handle);
    return result;
}

} // extern "C"
