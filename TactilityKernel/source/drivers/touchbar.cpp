// SPDX-License-Identifier: Apache-2.0
#include <tactility/concurrent/mutex.h>
#include <tactility/device.h>
#include <tactility/drivers/touchbar.h>
#include <tactility/error.h>

#define TOUCHBAR_DRIVER_API(driver) ((struct TouchbarApi*)driver->api)

static TouchbarEventSubscription* subscriptions = nullptr;

struct TouchbarEventMutex {
    Mutex handle {};
    TouchbarEventMutex() { mutex_construct(&handle); }
    ~TouchbarEventMutex() { mutex_destruct(&handle); }
};

static TouchbarEventMutex subscriptions_mutex;

extern "C" {

error_t touchbar_read_state(Device* device, TouchbarState* state) {
    if (!device_is_ready(device)) {
        *state = {};
        return ERROR_INVALID_STATE;
    }

    const auto* driver = device_get_driver(device);
    return TOUCHBAR_DRIVER_API(driver)->read_state(device, state);
}

error_t touchbar_recalibrate(Device* device) {
    if (!device_is_ready(device)) {
        return ERROR_INVALID_STATE;
    }

    const auto* driver = device_get_driver(device);
    if (TOUCHBAR_DRIVER_API(driver)->recalibrate == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }

    return TOUCHBAR_DRIVER_API(driver)->recalibrate(device);
}

bool touchbar_has_capability(Device* device, uint32_t capability) {
    const auto* driver = device_get_driver(device);
    const auto* api = TOUCHBAR_DRIVER_API(driver);
    if (api->has_capability != nullptr) {
        return api->has_capability(device, capability);
    }
    return (api->capabilities & capability) == capability;
}

void touchbar_emit_event(Device* device, TouchbarEvent event) {
    mutex_lock(&subscriptions_mutex.handle);
    for (TouchbarEventSubscription* sub = subscriptions; sub != nullptr; sub = sub->internal.next) {
        if (sub->internal.device != device) {
            continue;
        }

        // A finger sliding along the bar would otherwise flood the queue and push out the touch down/up events behind it
        if (event.type == TOUCHBAR_EVENT_MOVE && sub->internal.count > 0) {
            uint8_t newest = (sub->internal.head + sub->internal.count - 1) % TOUCHBAR_EVENT_QUEUE_CAPACITY;
            if (sub->internal.queue[newest].type == TOUCHBAR_EVENT_MOVE) {
                sub->internal.queue[newest] = event;
                continue;
            }
        }

        if (sub->internal.count >= TOUCHBAR_EVENT_QUEUE_CAPACITY) {
            continue;
        }
        uint8_t tail = (sub->internal.head + sub->internal.count) % TOUCHBAR_EVENT_QUEUE_CAPACITY;
        sub->internal.queue[tail] = event;
        sub->internal.count++;
    }
    mutex_unlock(&subscriptions_mutex.handle);
}

error_t touchbar_subscribe(Device* device, TouchbarEventSubscription* sub) {
    mutex_lock(&subscriptions_mutex.handle);

    // Avoid cyclic subscription list that would loop forever and be bad
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

error_t touchbar_unsubscribe(Device*, TouchbarEventSubscription* sub) {
    error_t result = ERROR_NOT_FOUND;

    mutex_lock(&subscriptions_mutex.handle);
    for (TouchbarEventSubscription** link = &subscriptions; *link != nullptr; link = &(*link)->internal.next) {
        if (*link == sub) {
            *link = sub->internal.next;
            result = ERROR_NONE;
            break;
        }
    }
    mutex_unlock(&subscriptions_mutex.handle);

    return result;
}

error_t touchbar_poll(Device*, TouchbarEventSubscription* sub, TouchbarEvent* out_event) {
    error_t result = ERROR_TIMEOUT;

    mutex_lock(&subscriptions_mutex.handle);
    if (sub->internal.count > 0) {
        *out_event = sub->internal.queue[sub->internal.head];
        sub->internal.head = (sub->internal.head + 1) % TOUCHBAR_EVENT_QUEUE_CAPACITY;
        sub->internal.count--;
        result = ERROR_NONE;
    }
    mutex_unlock(&subscriptions_mutex.handle);

    return result;
}

const DeviceType TOUCHBAR_TYPE {
    .name = "touchbar"
};

}
