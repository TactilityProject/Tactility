// SPDX-License-Identifier: Apache-2.0
#include <tactility/device.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/error.h>

#include <tactility/concurrent/mutex.h>

#define KEYBOARD_DRIVER_API(driver) ((struct KeyboardApi*)driver->api)

// Subscriptions, filtered by `device` on fan-out.
static KeyboardEventSubscription* subscriptions = nullptr;

struct KeyboardEventMutex {
    Mutex handle {};
    KeyboardEventMutex() { mutex_construct(&handle); }
    ~KeyboardEventMutex() { mutex_destruct(&handle); }
};

// One coarse mutex is fine: fan-out is just a struct copy, never caller code.
static KeyboardEventMutex subscriptions_mutex;

// Caller must hold subscriptions_mutex.
static void fan_out_to_subscribers(Device* device, const KeyboardKeyData& data) {
    for (KeyboardEventSubscription* sub = subscriptions; sub != nullptr; sub = sub->internal.next) {
        if (sub->internal.device != device) {
            continue;
        }
        if (sub->internal.count < KEYBOARD_EVENT_QUEUE_CAPACITY) {
            uint8_t tail = (sub->internal.head + sub->internal.count) % KEYBOARD_EVENT_QUEUE_CAPACITY;
            sub->internal.queue[tail] = data;
            sub->internal.count++;
        }
    }
}

static bool try_pop(KeyboardEventSubscription* sub, KeyboardKeyData* out_data) {
    mutex_lock(&subscriptions_mutex.handle);
    bool has_event = sub->internal.count > 0;
    if (has_event) {
        *out_data = sub->internal.queue[sub->internal.head];
        sub->internal.head = (sub->internal.head + 1) % KEYBOARD_EVENT_QUEUE_CAPACITY;
        sub->internal.count--;
    }
    mutex_unlock(&subscriptions_mutex.handle);
    return has_event;
}

extern "C" {

error_t keyboard_read_key(Device* device, KeyboardKeyData* data) {
    // Under the hotplug poller a keyboard device can be constructed but not started (probe
    // hasn't confirmed presence, or it's since detached) - a sync caller like LVGL's indev still
    // polls it every tick regardless, so bail out rather than touching the driver's (possibly
    // unallocated) state.
    if (!device_is_ready(device)) {
        *data = {};
        return ERROR_INVALID_STATE;
    }

    const auto* driver = device_get_driver(device);

    if (KEYBOARD_DRIVER_API(driver)->read_key == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }

    // Defaulted here, not per-driver: only drivers whose hardware reports them set them.
    data->ctrl = false;
    data->alt = false;
    data->hid_keycode = 0;
    data->hid_modifier = 0;

    error_t result = KEYBOARD_DRIVER_API(driver)->read_key(device, data);
    if (result == ERROR_NONE && data->key != 0) {
        // A sync caller (e.g. LVGL) isn't the only consumer of this key.
        mutex_lock(&subscriptions_mutex.handle);
        fan_out_to_subscribers(device, *data);
        mutex_unlock(&subscriptions_mutex.handle);
    }
    return result;
}

void keyboard_emit_key(Device* device, KeyboardKeyData data) {
    mutex_lock(&subscriptions_mutex.handle);
    fan_out_to_subscribers(device, data);
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
    if (try_pop(sub, out_data)) {
        return ERROR_NONE;
    }

    // keyboard_read_key() fans the result out to every subscriber, including `sub`.
    KeyboardKeyData data;
    if (keyboard_read_key(device, &data) != ERROR_NONE || data.key == 0) {
        return ERROR_TIMEOUT;
    }

    return try_pop(sub, out_data) ? ERROR_NONE : ERROR_TIMEOUT;
}

const DeviceType KEYBOARD_TYPE {
    .name = "keyboard",
};

}
