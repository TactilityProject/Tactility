// SPDX-License-Identifier: Apache-2.0
#include <app/event.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/time.h>

// Intrusive singly-linked list of subscriptions, keyed by app_instance_id (not broadcast by
// type, unlike TactilityKernel's system_event) - an app should only ever see events addressed
// to it. Guarded by a single coarse-grained mutex, same tradeoff system_event.cpp makes for its
// poll-subscription list: notifying a subscriber here never invokes caller code (just a struct
// copy and an xTaskNotifyGive), so there is no reentrancy concern requiring a snapshot-then-
// unlock dance.
static AppEventSubscription* subscriptions = nullptr;

struct AppEventMutex {
    Mutex handle {};
    AppEventMutex() { mutex_construct(&handle); }
    ~AppEventMutex() { mutex_destruct(&handle); }
};

static AppEventMutex subscriptions_mutex;

extern "C" {

error_t app_event_subscribe(AppEventSubscription* sub) {
    sub->task = xTaskGetCurrentTaskHandle();
    sub->head = 0;
    sub->count = 0;

    mutex_lock(&subscriptions_mutex.handle);
    sub->next = subscriptions;
    subscriptions = sub;
    mutex_unlock(&subscriptions_mutex.handle);

    return ERROR_NONE;
}

error_t app_event_unsubscribe(AppEventSubscription* sub) {
    error_t result = ERROR_NOT_FOUND;

    mutex_lock(&subscriptions_mutex.handle);
    for (AppEventSubscription** link = &subscriptions; *link != nullptr; link = &(*link)->next) {
        if (*link == sub) {
            *link = sub->next;
            result = ERROR_NONE;
            break;
        }
    }
    mutex_unlock(&subscriptions_mutex.handle);

    return result;
}

error_t app_event_emit(uint32_t app_instance_id, const AppEvent* event) {
    AppEvent stamped_event = *event;
    stamped_event.timestamp = get_micros_since_boot();

    error_t result = ERROR_NOT_FOUND;

    mutex_lock(&subscriptions_mutex.handle);
    for (AppEventSubscription* sub = subscriptions; sub != nullptr; sub = sub->next) {
        if (sub->app_instance_id != app_instance_id) {
            continue;
        }

        if (sub->count >= APP_EVENT_QUEUE_CAPACITY) {
            result = ERROR_RESOURCE;
            continue;
        }

        uint8_t tail = (sub->head + sub->count) % APP_EVENT_QUEUE_CAPACITY;
        sub->queue[tail] = stamped_event;
        sub->count++;
        if (result != ERROR_RESOURCE) {
            result = ERROR_NONE;
        }
        xTaskNotifyGive(sub->task);
    }
    mutex_unlock(&subscriptions_mutex.handle);

    return result;
}

static bool try_pop(AppEventSubscription* sub, AppEvent* out_event) {
    mutex_lock(&subscriptions_mutex.handle);
    bool has_event = sub->count > 0;
    if (has_event) {
        *out_event = sub->queue[sub->head];
        sub->head = (sub->head + 1) % APP_EVENT_QUEUE_CAPACITY;
        sub->count--;
    }
    mutex_unlock(&subscriptions_mutex.handle);
    return has_event;
}

error_t app_event_await(AppEventSubscription* sub, AppEvent* out_event, TickType_t timeout) {
    if (try_pop(sub, out_event)) {
        // Drain any notification credit this (or an earlier) push accumulated on this task's
        // FreeRTOS notification value: each app_event_emit() calls xTaskNotifyGive() regardless
        // of whether the consumer takes this fast path or the blocking path below, so without
        // this the credit would carry over and cause a future ulTaskNotifyTake() below to
        // return immediately for a notification that was already accounted for here.
        ulTaskNotifyTake(pdTRUE, 0);
        return ERROR_NONE;
    }

    if (ulTaskNotifyTake(pdTRUE, timeout) == 0) {
        return ERROR_TIMEOUT;
    }

    // Single-consumer by design (one task per subscription), so a wakeup implies the event
    // this call was notified for is still there for us to pop.
    return try_pop(sub, out_event) ? ERROR_NONE : ERROR_TIMEOUT;
}

} // extern "C"
