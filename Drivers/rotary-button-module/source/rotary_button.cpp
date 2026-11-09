// SPDX-License-Identifier: Apache-2.0
#include <drivers/rotary_button.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/gpio_descriptor.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/log.h>
#include <tactility/time.h>
#include <tactility/Memory.hpp>

#define TAG "rotary_button"
#define GET_CONFIG(device) (static_cast<const RotaryButtonConfig*>((device)->config))
#define GET_INTERNAL(device) (static_cast<RotaryButtonInternal*>(device_get_driver_data(device)))

static constexpr int BUTTON_COUNT = 4;
// A press and a release of every button within one read_key() interval
static constexpr int PENDING_CAPACITY = BUTTON_COUNT * 2;

struct RotaryButtonPendingEvent {
    uint32_t key;
    bool pressed;
};

struct RotaryButtonState {
    GpioDescriptor* descriptor = nullptr;
    uint32_t key = 0;
    bool pressed = false;
    uint32_t last_change_time = 0;
};

struct RotaryButtonInternal {
    RotaryButtonState buttons[BUTTON_COUNT];
    RotaryButtonPendingEvent pending[PENDING_CAPACITY];
    uint8_t pending_head = 0;
    uint8_t pending_count = 0;
    // LVGL only detects a long press when every read reports the key as pressed
    bool enter_held = false;
};

static bool push_pending(RotaryButtonInternal* internal, uint32_t key, bool pressed) {
    if (internal->pending_count >= PENDING_CAPACITY) {
        LOG_W(TAG, "Pending event queue full, dropping event");
        return false;
    }
    const uint8_t tail = (internal->pending_head + internal->pending_count) % PENDING_CAPACITY;
    internal->pending[tail] = { .key = key, .pressed = pressed };
    internal->pending_count++;
    return true;
}

static bool pop_pending(RotaryButtonInternal* internal, RotaryButtonPendingEvent* out_event) {
    if (internal->pending_count == 0) {
        return false;
    }
    *out_event = internal->pending[internal->pending_head];
    internal->pending_head = (internal->pending_head + 1) % PENDING_CAPACITY;
    internal->pending_count--;
    return true;
}

extern "C" {

// region Driver lifecycle

static void release_buttons(RotaryButtonInternal* internal) {
    for (auto& button : internal->buttons) {
        if (button.descriptor != nullptr) {
            gpio_descriptor_release(button.descriptor);
        }
    }
}

static error_t acquire_button(const GpioPinSpec& pin, uint32_t key, RotaryButtonState* out_state) {
    out_state->key = key;
    if (pin.gpio_controller == nullptr) {
        return ERROR_NONE;
    }
    out_state->descriptor = gpio_descriptor_acquire(pin.gpio_controller, pin.pin, pin.flags | GPIO_FLAG_DIRECTION_INPUT, GPIO_OWNER_GPIO);
    if (out_state->descriptor == nullptr) {
        LOG_E(TAG, "Failed to acquire pin %u", static_cast<unsigned>(pin.pin));
        return ERROR_RESOURCE;
    }
    return ERROR_NONE;
}

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);

    auto* internal = tt::newObject<RotaryButtonInternal>();
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    const GpioPinSpec* pins[BUTTON_COUNT] = { &config->pin_up, &config->pin_down, &config->pin_press, &config->pin_back };
    const uint32_t keys[BUTTON_COUNT] = { CODEPOINT_FOCUS_PREVIOUS, CODEPOINT_FOCUS_NEXT, CODEPOINT_ENTER, CODEPOINT_ESCAPE };
    for (int i = 0; i < BUTTON_COUNT; i++) {
        const error_t error = acquire_button(*pins[i], keys[i], &internal->buttons[i]);
        if (error != ERROR_NONE) {
            release_buttons(internal);
            tt::deleteObject(internal);
            return error;
        }
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = GET_INTERNAL(device);
    release_buttons(internal);
    device_set_driver_data(device, nullptr);
    tt::deleteObject(internal);
    return ERROR_NONE;
}

// endregion

// region KeyboardApi

static void poll_button(const RotaryButtonConfig* config, RotaryButtonInternal* internal, RotaryButtonState* button) {
    if (button->descriptor == nullptr) {
        return;
    }

    bool pressed = false;
    if (gpio_descriptor_get_level(button->descriptor, &pressed) != ERROR_NONE || pressed == button->pressed) {
        return;
    }

    const uint32_t now = get_millis();
    if ((now - button->last_change_time) < config->debounce_ms) {
        return;
    }

    // The state only changes once its event is queued, so a full queue retries it on the next poll
    if (push_pending(internal, button->key, pressed)) {
        button->pressed = pressed;
        button->last_change_time = now;
    }
}

static error_t rotary_button_read_key(Device* device, KeyboardKeyData* data) {
    const auto* config = GET_CONFIG(device);
    auto* internal = GET_INTERNAL(device);

    for (auto& button : internal->buttons) {
        poll_button(config, internal, &button);
    }

    RotaryButtonPendingEvent event;
    if (pop_pending(internal, &event)) {
        data->key = event.key;
        data->pressed = event.pressed;
        data->continue_reading = internal->pending_count > 0;
        if (event.key == CODEPOINT_ENTER) {
            internal->enter_held = event.pressed;
        }
    } else if (internal->enter_held) {
        data->key = CODEPOINT_ENTER;
        data->pressed = true;
        data->continue_reading = false;
    } else {
        data->key = 0;
        data->pressed = false;
        data->continue_reading = false;
    }

    return ERROR_NONE;
}

// endregion

static constexpr KeyboardApi ROTARY_BUTTON_API = {
    .read_key = rotary_button_read_key,
    .get_backlight = nullptr,
    .is_present = nullptr,
};

Driver rotary_button_driver = {
    .name = "rotary_button",
    .compatible = (const char*[]) { "tactility,rotary-button", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = &ROTARY_BUTTON_API,
    .device_type = &KEYBOARD_TYPE,
    .internal = nullptr
};

}
