// SPDX-License-Identifier: Apache-2.0
#include <tactility/device.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/error.h>

#include <tactility/concurrent/mutex.h>

#define KEYBOARD_DRIVER_API(driver) ((struct KeyboardApi*)driver->api)

/**
 * Intrusive singly-linked list of subscriptions, filtered by `device` on fan-out. Guarded by a
 * single coarse-grained mutex - same rationale as app_event.cpp's subscriptions_mutex: fanning
 * out here is just a struct copy, never caller code.
 */
static KeyboardEventSubscription* subscriptions = nullptr;

struct KeyboardEventMutex {
    Mutex handle {};
    KeyboardEventMutex() { mutex_construct(&handle); }
    ~KeyboardEventMutex() { mutex_destruct(&handle); }
};

static KeyboardEventMutex subscriptions_mutex;

// Pushes `data` onto every subscription for `device` except `exclude` (pass nullptr to include
// all of them). Caller must hold subscriptions_mutex.
static void fan_out_to_subscribers(Device* device, const KeyboardKeyData& data, const KeyboardEventSubscription* exclude) {
    for (KeyboardEventSubscription* sub = subscriptions; sub != nullptr; sub = sub->internal.next) {
        if (sub == exclude || sub->internal.device != device) {
            continue;
        }
        if (sub->internal.count < KEYBOARD_EVENT_QUEUE_CAPACITY) {
            uint8_t tail = (sub->internal.head + sub->internal.count) % KEYBOARD_EVENT_QUEUE_CAPACITY;
            sub->internal.queue[tail] = data;
            sub->internal.count++;
        }
    }
}

extern "C" {

error_t keyboard_read_key(Device* device, KeyboardKeyData* data) {
    const auto* driver = device_get_driver(device);

    if (KEYBOARD_DRIVER_API(driver)->read_key == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }

    // Default the modifier/HID fields here rather than in each driver: only drivers whose hardware
    // can report them set them, and the rest would otherwise leave whatever the caller's stack held.
    data->ctrl = false;
    data->alt = false;
    data->hid_keycode = 0;
    data->hid_modifier = 0;

    return KEYBOARD_DRIVER_API(driver)->read_key(device, data);
}

void keyboard_emit_key(Device* device, KeyboardKeyData data) {
    mutex_lock(&subscriptions_mutex.handle);
    fan_out_to_subscribers(device, data, nullptr);
    mutex_unlock(&subscriptions_mutex.handle);
}

error_t keyboard_get_backlight(Device* device, Device** backlight_device) {
    const auto* driver = device_get_driver(device);

    if (KEYBOARD_DRIVER_API(driver)->get_backlight == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }

    return KEYBOARD_DRIVER_API(driver)->get_backlight(device, backlight_device);
}

bool keyboard_is_present(Device* device) {
    const auto* driver = device_get_driver(device);

    if (KEYBOARD_DRIVER_API(driver)->is_present == nullptr) {
        return true;
    }

    return KEYBOARD_DRIVER_API(driver)->is_present(device);
}

error_t keyboard_subscribe(Device* device, KeyboardEventSubscription* sub) {
    mutex_lock(&subscriptions_mutex.handle);

    // Avoid cyclic subscription list that would loop forever
    if (subscriptions == sub) {
        mutex_unlock(&subscriptions_mutex.handle);
        return ERROR_INVALID_STATE;
    }

    sub->internal.device = device;
    sub->internal.head = 0;
    sub->internal.count = 0;
    sub->internal.next = subscriptions;
    subscriptions = sub;

    mutex_unlock(&subscriptions_mutex.handle);
    return ERROR_NONE;
}

error_t keyboard_unsubscribe(Device*, KeyboardEventSubscription* sub) {
    error_t result = ERROR_NOT_FOUND;

    mutex_lock(&subscriptions_mutex.handle);
    for (KeyboardEventSubscription** link = &subscriptions; *link != nullptr; link = &(*link)->internal.next) {
        if (*link == sub) {
            *link = sub->internal.next;
            result = ERROR_NONE;
            break;
        }
    }
    mutex_unlock(&subscriptions_mutex.handle);

    return result;
}

error_t keyboard_poll(Device* device, KeyboardEventSubscription* sub, KeyboardKeyData* out_data) {
    mutex_lock(&subscriptions_mutex.handle);

    if (sub->internal.count > 0) {
        *out_data = sub->internal.queue[sub->internal.head];
        sub->internal.head = (sub->internal.head + 1) % KEYBOARD_EVENT_QUEUE_CAPACITY;
        sub->internal.count--;
        mutex_unlock(&subscriptions_mutex.handle);
        return ERROR_NONE;
    }

    // No queued event of our own - try a synchronous read (if the driver has one; a driver that
    // only pushes via keyboard_emit_key() has no read_key, and there's simply nothing new yet).
    KeyboardKeyData data;
    if (keyboard_read_key(device, &data) != ERROR_NONE || data.key == 0) {
        mutex_unlock(&subscriptions_mutex.handle);
        return ERROR_TIMEOUT;
    }

    fan_out_to_subscribers(device, data, sub);

    *out_data = data;
    mutex_unlock(&subscriptions_mutex.handle);
    return ERROR_NONE;
}

const DeviceType KEYBOARD_TYPE {
    .name = "keyboard",
};

}
