// SPDX-License-Identifier: Apache-2.0
#include <tactility/concurrent/mutex.h>
#include <tactility/device.h>
#include <tactility/drivers/button.h>
#include <tactility/error.h>

#define BUTTON_DRIVER_API(driver) ((struct ButtonApi*)driver->api)

static ButtonEventSubscription* subscriptions = nullptr;

struct ButtonEventMutex {
    Mutex handle {};
    ButtonEventMutex() { mutex_construct(&handle); }
    ~ButtonEventMutex() { mutex_destruct(&handle); }
};

static ButtonEventMutex subscriptions_mutex;

struct ButtonCallbackEntry {
    button_event_callback_t callback;
    void* context;
};

static ButtonCallbackEntry callbacks[BUTTON_CALLBACK_CAPACITY];
static uint8_t callback_count = 0;

extern "C" {

void button_emit_event(Device* device, ButtonEvent event) {
    mutex_lock(&subscriptions_mutex.handle);
    for (ButtonEventSubscription* sub = subscriptions; sub != nullptr; sub = sub->internal.next) {
        if (sub->internal.device != device || sub->internal.count >= BUTTON_EVENT_QUEUE_CAPACITY) {
            continue;
        }
        uint8_t tail = (sub->internal.head + sub->internal.count) % BUTTON_EVENT_QUEUE_CAPACITY;
        sub->internal.queue[tail] = event;
        sub->internal.count++;
    }

    // The callbacks are copied, so they can add or remove callbacks without deadlocking
    ButtonCallbackEntry snapshot[BUTTON_CALLBACK_CAPACITY];
    const uint8_t snapshot_count = callback_count;
    for (uint8_t i = 0; i < snapshot_count; i++) {
        snapshot[i] = callbacks[i];
    }
    mutex_unlock(&subscriptions_mutex.handle);

    for (uint8_t i = 0; i < snapshot_count; i++) {
        snapshot[i].callback(device, &event, snapshot[i].context);
    }
}

error_t button_callback_add(button_event_callback_t callback, void* context) {
    mutex_lock(&subscriptions_mutex.handle);
    error_t result = ERROR_RESOURCE;
    if (callback_count < BUTTON_CALLBACK_CAPACITY) {
        callbacks[callback_count++] = ButtonCallbackEntry { callback, context };
        result = ERROR_NONE;
    }
    mutex_unlock(&subscriptions_mutex.handle);
    return result;
}

error_t button_callback_remove(button_event_callback_t callback) {
    error_t result = ERROR_NOT_FOUND;
    mutex_lock(&subscriptions_mutex.handle);
    for (uint8_t i = 0; i < callback_count; i++) {
        if (callbacks[i].callback == callback) {
            for (uint8_t j = i + 1; j < callback_count; j++) {
                callbacks[j - 1] = callbacks[j];
            }
            callback_count--;
            result = ERROR_NONE;
            break;
        }
    }
    mutex_unlock(&subscriptions_mutex.handle);
    return result;
}

error_t button_is_pressed(Device* device, bool* pressed) {
    const auto* driver = device_get_driver(device);
    if (BUTTON_DRIVER_API(driver)->is_pressed == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    return BUTTON_DRIVER_API(driver)->is_pressed(device, pressed);
}

error_t button_subscribe(Device* device, ButtonEventSubscription* sub) {
    mutex_lock(&subscriptions_mutex.handle);

    for (auto* current = subscriptions; current != nullptr; current = current->internal.next) {
        if (current == sub) {
            mutex_unlock(&subscriptions_mutex.handle);
            return ERROR_INVALID_STATE;
        }
    }

    sub->internal.device = device;
    sub->internal.head = 0;
    sub->internal.count = 0;
    sub->internal.next = subscriptions;
    subscriptions = sub;

    mutex_unlock(&subscriptions_mutex.handle);
    return ERROR_NONE;
}

error_t button_unsubscribe(Device*, ButtonEventSubscription* sub) {
    error_t result = ERROR_NOT_FOUND;

    mutex_lock(&subscriptions_mutex.handle);
    for (ButtonEventSubscription** link = &subscriptions; *link != nullptr; link = &(*link)->internal.next) {
        if (*link == sub) {
            *link = sub->internal.next;
            result = ERROR_NONE;
            break;
        }
    }
    mutex_unlock(&subscriptions_mutex.handle);

    return result;
}

error_t button_poll(Device*, ButtonEventSubscription* sub, ButtonEvent* out_event) {
    error_t result = ERROR_TIMEOUT;

    mutex_lock(&subscriptions_mutex.handle);
    if (sub->internal.count > 0) {
        *out_event = sub->internal.queue[sub->internal.head];
        sub->internal.head = (sub->internal.head + 1) % BUTTON_EVENT_QUEUE_CAPACITY;
        sub->internal.count--;
        result = ERROR_NONE;
    }
    mutex_unlock(&subscriptions_mutex.handle);

    return result;
}

const DeviceType BUTTON_TYPE {
    .name = "button",
};

}
