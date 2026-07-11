// SPDX-License-Identifier: Apache-2.0
#include <drivers/tdeck_trackball.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/gpio_descriptor.h>
#include <tactility/log.h>

#include <atomic>
#include <new>

#define TAG "tdeck_trackball"
#define GET_CONFIG(device) (static_cast<const TdeckTrackballConfig*>((device)->config))
#define GET_INTERNAL(device) (static_cast<TdeckTrackballInternal*>(device_get_driver_data(device)))

struct TdeckTrackballInternal {
    GpioDescriptor* pin_right = nullptr;
    GpioDescriptor* pin_up = nullptr;
    GpioDescriptor* pin_left = nullptr;
    GpioDescriptor* pin_down = nullptr;
    GpioDescriptor* pin_click = nullptr;

    std::atomic<int32_t> dx {0};
    std::atomic<int32_t> dy {0};
    std::atomic<bool> button_pressed {false};
};

// region ISR callbacks

static void on_right(void* arg) {
    static_cast<TdeckTrackballInternal*>(arg)->dx.fetch_add(1, std::memory_order_relaxed);
}

static void on_left(void* arg) {
    static_cast<TdeckTrackballInternal*>(arg)->dx.fetch_sub(1, std::memory_order_relaxed);
}

static void on_down(void* arg) {
    static_cast<TdeckTrackballInternal*>(arg)->dy.fetch_add(1, std::memory_order_relaxed);
}

static void on_up(void* arg) {
    static_cast<TdeckTrackballInternal*>(arg)->dy.fetch_sub(1, std::memory_order_relaxed);
}

static void on_click(void* arg) {
    auto* internal = static_cast<TdeckTrackballInternal*>(arg);
    bool high = true;
    gpio_descriptor_get_level(internal->pin_click, &high);
    // Active low: pressed when level is low
    internal->button_pressed.store(!high, std::memory_order_relaxed);
}

// endregion

// region Pin acquisition

static error_t acquire_pin(const GpioPinSpec& spec, GpioInterruptType interrupt_type, void (*callback)(void*), void* arg, GpioDescriptor** out_descriptor) {
    auto* descriptor = gpio_descriptor_acquire(spec.gpio_controller, spec.pin, GPIO_OWNER_GPIO);
    if (descriptor == nullptr) {
        return ERROR_RESOURCE;
    }

    gpio_flags_t flags = GPIO_FLAG_DIRECTION_INPUT | GPIO_FLAG_PULL_UP;
    flags = GPIO_FLAG_INTERRUPT_TO_OPTIONS(flags, interrupt_type);

    error_t error = gpio_descriptor_set_flags(descriptor, flags);
    if (error == ERROR_NONE) {
        error = gpio_descriptor_add_callback(descriptor, callback, arg);
    }
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

static void release_all_pins(TdeckTrackballInternal* internal) {
    release_pin(internal->pin_right);
    release_pin(internal->pin_up);
    release_pin(internal->pin_left);
    release_pin(internal->pin_down);
    release_pin(internal->pin_click);
}

// endregion

extern "C" {

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

error_t tdeck_trackball_read_delta(Device* device, int32_t* out_dx, int32_t* out_dy) {
    return read_delta(device, out_dx, out_dy);
}

error_t tdeck_trackball_get_button_pressed(Device* device, bool* out_pressed) {
    return get_button_pressed(device, out_pressed);
}

static error_t start(Device* device) {
    LOG_I(TAG, "start %s", device->name);
    auto* config = GET_CONFIG(device);

    auto* internal = new(std::nothrow) TdeckTrackballInternal();
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
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    LOG_I(TAG, "stop %s", device->name);
    auto* internal = GET_INTERNAL(device);
    release_all_pins(internal);
    device_set_driver_data(device, nullptr);
    delete internal;
    return ERROR_NONE;
}

static constexpr TdeckTrackballApi TDECK_TRACKBALL_API = {
    .read_delta = read_delta,
    .get_button_pressed = get_button_pressed,
};

const struct DeviceType TDECK_TRACKBALL_TYPE {
    .name = "tdeck-trackball"
};

extern Module lilygo_tdeck_module;

Driver tdeck_trackball_driver = {
    .name = "tdeck_trackball",
    .compatible = (const char*[]) { "lilygo,tdeck-trackball", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &TDECK_TRACKBALL_API,
    .device_type = &TDECK_TRACKBALL_TYPE,
    .owner = &lilygo_tdeck_module,
    .internal = nullptr
};

}
