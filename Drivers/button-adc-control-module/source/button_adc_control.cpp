// SPDX-License-Identifier: Apache-2.0
#include <drivers/button_adc_control.h>

#include <button_adc_control_module.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/adc_controller.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/time.h>

#include <cstdlib>

#define TAG "ButtonAdcControl"
#define GET_CONFIG(device) (static_cast<const ButtonAdcControlConfig*>((device)->config))

// Worst case: every button on the ladder completes a press+release pair within one
// read_key() poll interval. Ladders cap at a handful of buttons, so 16 never binds.
constexpr auto BUTTON_ADC_PENDING_CAPACITY = 16;

struct ButtonAdcPendingEvent {
    uint32_t key;
    bool pressed;
};

struct ButtonAdcButtonState {
    bool in_use;
    bool debounced_pressed;
    uint32_t last_change_time;
};

struct ButtonAdcInternal {
    ButtonAdcButtonState* button_states;
    ButtonAdcPendingEvent pending[BUTTON_ADC_PENDING_CAPACITY];
    uint8_t pending_head;
    uint8_t pending_count;
};

static void push_pending(ButtonAdcInternal* internal, uint32_t key, bool pressed) {
    if (internal->pending_count >= BUTTON_ADC_PENDING_CAPACITY) {
        LOG_W(TAG, "Pending event queue full, dropping event");
        return;
    }
    uint8_t tail = (internal->pending_head + internal->pending_count) % BUTTON_ADC_PENDING_CAPACITY;
    internal->pending[tail] = { .key = key, .pressed = pressed };
    internal->pending_count++;
}

static bool pop_pending(ButtonAdcInternal* internal, ButtonAdcPendingEvent* out_event) {
    if (internal->pending_count == 0) {
        return false;
    }
    *out_event = internal->pending[internal->pending_head];
    internal->pending_head = (internal->pending_head + 1) % BUTTON_ADC_PENDING_CAPACITY;
    internal->pending_count--;
    return true;
}

// region Driver lifecycle

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);

    auto* internal = static_cast<ButtonAdcInternal*>(malloc(sizeof(ButtonAdcInternal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    *internal = {};

    internal->button_states = static_cast<ButtonAdcButtonState*>(calloc(config->buttons_count, sizeof(ButtonAdcButtonState)));
    if (internal->button_states == nullptr) {
        free(internal);
        return ERROR_OUT_OF_MEMORY;
    }

    for (size_t i = 0; i < config->buttons_count; ++i) {
        internal->button_states[i].in_use = config->buttons[i].adc_controller != nullptr;
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<ButtonAdcInternal*>(device_get_driver_data(device));

    free(internal->button_states);
    free(internal);
    return ERROR_NONE;
}

// endregion

// region KeyboardApi

static void poll_button(const ButtonAdcControlConfig* config, ButtonAdcInternal* internal, size_t button_index) {
    auto& state = internal->button_states[button_index];
    if (!state.in_use) {
        return;
    }

    const auto& button = config->buttons[button_index];

    int raw;
    AdcChannelSpec channel_spec = { button.adc_controller, button.channel };
    if (adc_channel_read_raw(&channel_spec, &raw, portMAX_DELAY) != ERROR_NONE) {
        return;
    }

    bool raw_pressed = raw > button.band_low && raw <= button.band_high;

    uint32_t now = get_millis();
    if ((now - state.last_change_time) < config->debounce_ms) {
        return;
    }

    if (raw_pressed == state.debounced_pressed) {
        return;
    }
    state.last_change_time = now;
    state.debounced_pressed = raw_pressed;

    push_pending(internal, button.key, raw_pressed);
}

static error_t button_adc_control_read_key(Device* device, KeyboardKeyData* data) {
    const auto* config = GET_CONFIG(device);
    auto* internal = static_cast<ButtonAdcInternal*>(device_get_driver_data(device));

    for (size_t i = 0; i < config->buttons_count; ++i) {
        poll_button(config, internal, i);
    }

    ButtonAdcPendingEvent event;
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

static constexpr KeyboardApi button_adc_control_api = {
    .read_key = button_adc_control_read_key,
};

Driver button_adc_control_driver = {
    .name = "button_adc_control",
    .compatible = (const char*[]) { "tactility,button-adc-control", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &button_adc_control_api,
    .device_type = &KEYBOARD_TYPE,
    .owner = &button_adc_control_module,
    .internal = nullptr
};
