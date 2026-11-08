// SPDX-License-Identifier: Apache-2.0
#include "gpio_trackball_child.h"

#include <drivers/gpio_trackball.h>

#include <tactility/check.h>
#include <tactility/concurrent/mutex.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/gpio_descriptor.h>
#include <tactility/drivers/trackball.h>
#include <tactility/log.h>

#include <atomic>
#include <new>

constexpr auto* TAG = "gpio_trackball";

#define GET_CONFIG(device) (static_cast<const GpioTrackballConfig*>((device)->config))
#define GET_INTERNAL(device) (static_cast<GpioTrackballInternal*>(device_get_driver_data(device)))

struct GpioTrackballInternal {
    GpioDescriptor* pin_right = nullptr;
    GpioDescriptor* pin_up = nullptr;
    GpioDescriptor* pin_left = nullptr;
    GpioDescriptor* pin_down = nullptr;
    GpioDescriptor* pin_click = nullptr;

    std::atomic<int32_t> dx {0};
    std::atomic<int32_t> dy {0};
    std::atomic<bool> button_pressed {false};

    // Guards settings and the child device. Not device_lock(): replacing a child takes the ledger lock,
    // and lookups take the ledger lock first and then a device's lock.
    Mutex settings_mutex {};
    TrackballSettings settings = trackball_settings_get_default();
    Device* child = nullptr;
    GpioTrackballChildState* child_state = nullptr;
    // The child is stopped and removed, but is still referenced so it can't be destructed yet
    bool child_detached = false;
    // Set by stop() once the child is gone, so a settings change that was waiting for the lock doesn't create a new one
    bool stopped = false;

    GpioTrackballInternal() { mutex_construct(&settings_mutex); }
    ~GpioTrackballInternal() { mutex_destruct(&settings_mutex); }
};

// region ISR callbacks

static void on_right(void* arg) {
    static_cast<GpioTrackballInternal*>(arg)->dx.fetch_add(1, std::memory_order_relaxed);
}

static void on_left(void* arg) {
    static_cast<GpioTrackballInternal*>(arg)->dx.fetch_sub(1, std::memory_order_relaxed);
}

static void on_down(void* arg) {
    static_cast<GpioTrackballInternal*>(arg)->dy.fetch_add(1, std::memory_order_relaxed);
}

static void on_up(void* arg) {
    static_cast<GpioTrackballInternal*>(arg)->dy.fetch_sub(1, std::memory_order_relaxed);
}

static void on_click(void* arg) {
    auto* internal = static_cast<GpioTrackballInternal*>(arg);
    bool high = true;
    gpio_descriptor_get_level(internal->pin_click, &high);
    // Active low: pressed when level is low
    internal->button_pressed.store(!high, std::memory_order_relaxed);
}

// endregion

// region Pin acquisition

static error_t acquire_pin(const GpioPinSpec& spec, GpioInterruptType interrupt_type, void (*callback)(void*), void* arg, GpioDescriptor** out_descriptor) {
    gpio_flags_t flags = GPIO_FLAG_DIRECTION_INPUT | GPIO_FLAG_PULL_UP;
    flags = GPIO_FLAG_INTERRUPT_TO_OPTIONS(flags, interrupt_type);

    auto* descriptor = gpio_descriptor_acquire(spec.gpio_controller, spec.pin, flags, GPIO_OWNER_GPIO);
    if (descriptor == nullptr) {
        return ERROR_RESOURCE;
    }

    error_t error = gpio_descriptor_add_callback(descriptor, callback, arg);
    if (error == ERROR_NONE) {
        error = gpio_descriptor_enable_interrupt(descriptor);
    }

    if (error != ERROR_NONE) {
        gpio_descriptor_remove_callback(descriptor);
        gpio_descriptor_release(descriptor);
        return error;
    }

    *out_descriptor = descriptor;
    return ERROR_NONE;
}

static void release_pin(GpioDescriptor*& descriptor) {
    if (descriptor == nullptr) {
        return;
    }
    gpio_descriptor_disable_interrupt(descriptor);
    gpio_descriptor_remove_callback(descriptor);
    gpio_descriptor_release(descriptor);
    descriptor = nullptr;
}

static void release_all_pins(GpioTrackballInternal* internal) {
    release_pin(internal->pin_right);
    release_pin(internal->pin_up);
    release_pin(internal->pin_left);
    release_pin(internal->pin_down);
    release_pin(internal->pin_click);
}

// endregion

// region Child device

static const char* child_name(const TrackballSettings& settings) {
    return settings.mode == TRACKBALL_MODE_MOUSE ? "trackball-mouse" : "trackball-keyboard";
}

/** @retval ERROR_RESOURCE_BUSY when the child is still referenced: it is kept (stopped and removed), and a next call destructs it */
static error_t destroy_child(GpioTrackballInternal* internal) {
    if (internal->child == nullptr) {
        return ERROR_NONE;
    }
    if (!internal->child_detached) {
        check(device_stop(internal->child) == ERROR_NONE);
        check(device_remove(internal->child) == ERROR_NONE);
        internal->child_detached = true;
    }
    error_t error = device_destruct(internal->child);
    if (error != ERROR_NONE) {
        return error;
    }
    delete internal->child;
    delete internal->child_state;
    internal->child = nullptr;
    internal->child_state = nullptr;
    internal->child_detached = false;
    return ERROR_NONE;
}

static error_t create_child(Device* parent, GpioTrackballInternal* internal, const TrackballSettings& settings) {
    auto* state = new(std::nothrow) GpioTrackballChildState();
    auto* child = new(std::nothrow) Device { .address = 0, .name = child_name(settings), .config = state, .parent = nullptr, .flags = 0, .internal = nullptr };
    if (state == nullptr || child == nullptr) {
        delete state;
        delete child;
        return ERROR_OUT_OF_MEMORY;
    }
    state->trackball = parent;
    state->sensitivity = settings.mode == TRACKBALL_MODE_MOUSE ? settings.pointer_sensitivity : settings.key_sensitivity;

    // Movement that was accumulated while there was no child isn't reported
    internal->dx.store(0, std::memory_order_relaxed);
    internal->dy.store(0, std::memory_order_relaxed);

    error_t error = device_construct(child);
    if (error != ERROR_NONE) {
        delete child;
        delete state;
        return error;
    }

    device_set_parent(child, parent);
    device_set_driver(child, settings.mode == TRACKBALL_MODE_MOUSE ? &gpio_trackball_mouse_driver : &gpio_trackball_keyboard_driver);

    error = device_add(child);
    if (error == ERROR_NONE) {
        error = device_start(child);
        if (error != ERROR_NONE) {
            device_remove(child);
        }
    }
    if (error != ERROR_NONE) {
        device_destruct(child);
        delete child;
        delete state;
        return error;
    }

    internal->child = child;
    internal->child_state = state;
    return ERROR_NONE;
}

static bool child_matches(const GpioTrackballInternal* internal, const TrackballSettings& settings) {
    if (internal->child_detached) {
        return false;
    }
    if (internal->child == nullptr) {
        return !settings.enabled;
    }
    return settings.enabled && device_get_driver(internal->child) == (settings.mode == TRACKBALL_MODE_MOUSE ? &gpio_trackball_mouse_driver : &gpio_trackball_keyboard_driver);
}

static error_t apply_settings(Device* device, GpioTrackballInternal* internal, const TrackballSettings& settings) {
    if (child_matches(internal, settings)) {
        if (internal->child_state != nullptr) {
            internal->child_state->sensitivity = settings.mode == TRACKBALL_MODE_MOUSE ? settings.pointer_sensitivity : settings.key_sensitivity;
        }
        internal->settings = settings;
        return ERROR_NONE;
    }

    error_t error = destroy_child(internal);
    if (error != ERROR_NONE) {
        return error;
    }
    internal->settings = settings;
    if (!settings.enabled) {
        return ERROR_NONE;
    }
    return create_child(device, internal, settings);
}

// endregion

extern "C" {

static error_t set_settings(Device* device, const TrackballSettings* settings) {
    if ((settings->mode != TRACKBALL_MODE_KEYBOARD && settings->mode != TRACKBALL_MODE_MOUSE) ||
        settings->key_sensitivity == 0 ||
        settings->pointer_sensitivity == 0
    ) {
        return ERROR_INVALID_ARGUMENT;
    }
    auto* internal = GET_INTERNAL(device);
    mutex_lock(&internal->settings_mutex);
    error_t error = internal->stopped ? ERROR_INVALID_STATE : apply_settings(device, internal, *settings);
    mutex_unlock(&internal->settings_mutex);
    return error;
}

static error_t get_settings(Device* device, TrackballSettings* out_settings) {
    auto* internal = GET_INTERNAL(device);
    mutex_lock(&internal->settings_mutex);
    *out_settings = internal->settings;
    mutex_unlock(&internal->settings_mutex);
    return ERROR_NONE;
}

static error_t read_delta(Device* device, int32_t* out_dx, int32_t* out_dy) {
    auto* internal = GET_INTERNAL(device);
    *out_dx = internal->dx.exchange(0, std::memory_order_relaxed);
    *out_dy = internal->dy.exchange(0, std::memory_order_relaxed);
    return ERROR_NONE;
}

static error_t get_button_pressed(Device* device, bool* out_pressed) {
    *out_pressed = GET_INTERNAL(device)->button_pressed.load(std::memory_order_relaxed);
    return ERROR_NONE;
}

static error_t start(Device* device) {
    LOG_I(TAG, "start %s", device->name);
    auto* config = GET_CONFIG(device);

    auto* internal = new(std::nothrow) GpioTrackballInternal();
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    error_t error = acquire_pin(config->pin_right, GPIO_INTERRUPT_NEG_EDGE, on_right, internal, &internal->pin_right);
    if (error == ERROR_NONE) {
        error = acquire_pin(config->pin_up, GPIO_INTERRUPT_NEG_EDGE, on_up, internal, &internal->pin_up);
    }
    if (error == ERROR_NONE) {
        error = acquire_pin(config->pin_left, GPIO_INTERRUPT_NEG_EDGE, on_left, internal, &internal->pin_left);
    }
    if (error == ERROR_NONE) {
        error = acquire_pin(config->pin_down, GPIO_INTERRUPT_NEG_EDGE, on_down, internal, &internal->pin_down);
    }
    if (error == ERROR_NONE) {
        error = acquire_pin(config->pin_click, GPIO_INTERRUPT_ANY_EDGE, on_click, internal, &internal->pin_click);
    }

    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to acquire trackball pins: %s", error_to_string(error));
        release_all_pins(internal);
        delete internal;
        return error;
    }

    // Read the click pin's initial level now that the descriptor is acquired.
    on_click(internal);

    device_set_driver_data(device, internal);

    internal->settings.mode = config->default_mode;
    error = apply_settings(device, internal, internal->settings);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to create child device: %s", error_to_string(error));
        device_set_driver_data(device, nullptr);
        release_all_pins(internal);
        delete internal;
        return error;
    }
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    LOG_I(TAG, "stop %s", device->name);
    auto* internal = GET_INTERNAL(device);
    mutex_lock(&internal->settings_mutex);
    error_t error = destroy_child(internal);
    if (error != ERROR_NONE) {
        mutex_unlock(&internal->settings_mutex);
        LOG_E(TAG, "Failed to destroy child device: %s", error_to_string(error));
        return error;
    }
    internal->stopped = true;
    release_all_pins(internal);
    device_set_driver_data(device, nullptr);
    mutex_unlock(&internal->settings_mutex);
    delete internal;
    return ERROR_NONE;
}

static constexpr TrackballApi GPIO_TRACKBALL_API = {
    .read_delta = read_delta,
    .get_button_pressed = get_button_pressed,
    .set_settings = set_settings,
    .get_settings = get_settings,
};

Driver gpio_trackball_driver = {
    .name = "gpio_trackball",
    .compatible = (const char*[]) { "tactility,gpio-trackball", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = &GPIO_TRACKBALL_API,
    .device_type = &TRACKBALL_TYPE,
    .internal = nullptr
};

}
