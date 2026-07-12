// SPDX-License-Identifier: Apache-2.0
#include <drivers/button_control.h>
#include <button_control_module.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/time.h>

#include <lvgl.h>

#include <cstdlib>

#define TAG "ButtonControl"
#define GET_CONFIG(device) (static_cast<const ButtonControlConfig*>((device)->config))

// Worst case: both buttons complete a short/long press gesture (press + release, 2 events each)
// within the same read_key() polling interval.
constexpr auto BUTTON_CONTROL_PENDING_CAPACITY = 4;

struct ButtonControlPendingEvent {
    uint32_t key;
    bool pressed;
};

struct ButtonControlButtonState {
    GpioDescriptor* descriptor;
    bool in_use;
    bool debounced_pressed;
    uint32_t press_start_time;
    uint32_t last_change_time;
    uint32_t short_press_key;
    uint32_t long_press_key;
};

struct ButtonControlInternal {
    ButtonControlButtonState primary;
    ButtonControlButtonState secondary;
    ButtonControlPendingEvent pending[BUTTON_CONTROL_PENDING_CAPACITY];
    uint8_t pending_head;
    uint8_t pending_count;
};

static void push_pending(ButtonControlInternal* internal, uint32_t key, bool pressed) {
    if (internal->pending_count >= BUTTON_CONTROL_PENDING_CAPACITY) {
        LOG_W(TAG, "Pending event queue full, dropping event");
        return;
    }
    uint8_t tail = (internal->pending_head + internal->pending_count) % BUTTON_CONTROL_PENDING_CAPACITY;
    internal->pending[tail] = { .key = key, .pressed = pressed };
    internal->pending_count++;
}

static bool pop_pending(ButtonControlInternal* internal, ButtonControlPendingEvent* out_event) {
    if (internal->pending_count == 0) {
        return false;
    }
    *out_event = internal->pending[internal->pending_head];
    internal->pending_head = (internal->pending_head + 1) % BUTTON_CONTROL_PENDING_CAPACITY;
    internal->pending_count--;
    return true;
}

// region Driver lifecycle

static error_t acquire_button(const GpioPinSpec& pin, uint32_t short_press_key, uint32_t long_press_key, ButtonControlButtonState* out_state) {
    if (pin.gpio_controller == nullptr) {
        *out_state = {};
        return ERROR_NONE;
    }

    auto* descriptor = gpio_descriptor_acquire(pin.gpio_controller, pin.pin, GPIO_OWNER_GPIO);
    if (descriptor == nullptr) {
        LOG_E(TAG, "Failed to acquire GPIO descriptor");
        return ERROR_RESOURCE;
    }

    if (gpio_descriptor_set_flags(descriptor, pin.flags | GPIO_FLAG_DIRECTION_INPUT) != ERROR_NONE) {
        LOG_E(TAG, "Failed to configure GPIO as input");
        gpio_descriptor_release(descriptor);
        return ERROR_RESOURCE;
    }

    *out_state = {
        .descriptor = descriptor,
        .in_use = true,
        .debounced_pressed = false,
        .press_start_time = 0,
        .last_change_time = 0,
        .short_press_key = short_press_key,
        .long_press_key = long_press_key,
    };
    return ERROR_NONE;
}

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);

    auto* internal = static_cast<ButtonControlInternal*>(malloc(sizeof(ButtonControlInternal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    *internal = {};

    bool two_button_mode = config->pin_secondary.gpio_controller != nullptr;

    error_t error = acquire_button(
        config->pin_primary,
        two_button_mode ? LV_KEY_ENTER : LV_KEY_NEXT,
        two_button_mode ? LV_KEY_ESC : LV_KEY_ENTER,
        &internal->primary
    );
    if (error != ERROR_NONE) {
        free(internal);
        return error;
    }

    error = acquire_button(config->pin_secondary, LV_KEY_NEXT, LV_KEY_PREV, &internal->secondary);
    if (error != ERROR_NONE) {
        if (internal->primary.in_use) {
            gpio_descriptor_release(internal->primary.descriptor);
        }
        free(internal);
        return error;
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<ButtonControlInternal*>(device_get_driver_data(device));

    if (internal->primary.in_use) {
        gpio_descriptor_release(internal->primary.descriptor);
    }
    if (internal->secondary.in_use) {
        gpio_descriptor_release(internal->secondary.descriptor);
    }

    free(internal);
    return ERROR_NONE;
}

// endregion

// region KeyboardApi

// Long press is only distinguished from short press at release time, by how long the button
// was held - not at the moment it's first pressed. This matches physical button behavior:
// you can't know a press is "long" until it either ends or the threshold is reached.
static void poll_button(const ButtonControlConfig* config, ButtonControlInternal* internal, ButtonControlButtonState* state) {
    if (!state->in_use) {
        return;
    }

    bool high = false;
    if (gpio_descriptor_get_level(state->descriptor, &high) != ERROR_NONE) {
        return;
    }
    bool raw_pressed = config->active_low ? !high : high;

    uint32_t now = get_millis();
    if ((now - state->last_change_time) < config->debounce_ms) {
        return;
    }

    if (raw_pressed == state->debounced_pressed) {
        return;
    }
    state->last_change_time = now;
    state->debounced_pressed = raw_pressed;

    if (raw_pressed) {
        state->press_start_time = now;
        return;
    }

    // Release: decide short vs. long press by elapsed hold duration, then queue a synthetic
    // key tap (press followed by release) for the LVGL key this gesture maps to.
    uint32_t held_ms = now - state->press_start_time;
    uint32_t key = held_ms < config->long_press_ms ? state->short_press_key : state->long_press_key;
    push_pending(internal, key, true);
    push_pending(internal, key, false);
}

static error_t button_control_read_key(Device* device, KeyboardKeyData* data) {
    const auto* config = GET_CONFIG(device);
    auto* internal = static_cast<ButtonControlInternal*>(device_get_driver_data(device));

    poll_button(config, internal, &internal->primary);
    poll_button(config, internal, &internal->secondary);

    ButtonControlPendingEvent event;
    if (pop_pending(internal, &event)) {
        data->key = event.key;
        data->pressed = event.pressed;
        data->continue_reading = internal->pending_count > 0;
    } else {
        data->key = 0;
        data->pressed = false;
        data->continue_reading = false;
    }

    return ERROR_NONE;
}

// endregion

static constexpr KeyboardApi button_control_api = {
    .read_key = button_control_read_key,
};

Driver button_control_driver = {
    .name = "button_control",
    .compatible = (const char*[]) { "tactility,button-control", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &button_control_api,
    .device_type = &KEYBOARD_TYPE,
    .owner = &button_control_module,
    .internal = nullptr
};
