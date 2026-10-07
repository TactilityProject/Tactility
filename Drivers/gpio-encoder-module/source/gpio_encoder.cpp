// SPDX-License-Identifier: Apache-2.0
#include <drivers/gpio_encoder.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/gpio_descriptor.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/log.h>

#include <driver/pulse_cnt.h>

#include <new>

#define TAG "gpio_encoder"
#define GET_CONFIG(device) (static_cast<const GpioEncoderConfig*>((device)->config))
#define GET_INTERNAL(device) (static_cast<GpioEncoderInternal*>(device_get_driver_data(device)))

struct GpioEncoderPendingEvent {
    uint32_t key;
    bool pressed;
};

struct GpioEncoderInternal {
    pcnt_unit_handle_t pcnt_unit = nullptr;
    GpioDescriptor* pin_a = nullptr;
    GpioDescriptor* pin_b = nullptr;
    GpioDescriptor* pin_enter = nullptr;
    int32_t pulse_remainder = 0;
    bool button_pressed = false;
    int32_t pulses_per_detent = 0;
    GpioEncoderPendingEvent* pending = nullptr;
    uint32_t pending_capacity = 0;
    uint32_t pending_head = 0;
    uint32_t pending_count = 0;
};

static bool push_pending(GpioEncoderInternal* internal, uint32_t key, bool pressed) {
    if (internal->pending_count >= internal->pending_capacity) {
        LOG_W(TAG, "Pending event queue full, dropping event");
        return false;
    }
    uint32_t tail = (internal->pending_head + internal->pending_count) % internal->pending_capacity;
    internal->pending[tail] = { .key = key, .pressed = pressed };
    internal->pending_count++;
    return true;
}

static bool pop_pending(GpioEncoderInternal* internal, GpioEncoderPendingEvent* out_event) {
    if (internal->pending_count == 0) {
        return false;
    }
    *out_event = internal->pending[internal->pending_head];
    internal->pending_head = (internal->pending_head + 1) % internal->pending_capacity;
    internal->pending_count--;
    return true;
}

extern "C" {

// region Driver lifecycle

// Accumulating count makes over-/underflow automatically compensated; requires watch points at
// the low and high limits (see pcnt_unit_add_watch_point() below). Ported from the deprecated
// HAL's TpagerEncoder::initEncoder().
static constexpr int PCNT_LOW_LIMIT = -127;
static constexpr int PCNT_HIGH_LIMIT = 126;

static error_t init_pcnt_unit(int pin_a, int pin_b, pcnt_unit_handle_t* out_unit) {
    pcnt_unit_config_t unit_config = {
        .low_limit = PCNT_LOW_LIMIT,
        .high_limit = PCNT_HIGH_LIMIT,
        .intr_priority = 0,
        .flags = { .accum_count = 1 },
    };

    pcnt_unit_handle_t unit = nullptr;
    if (pcnt_new_unit(&unit_config, &unit) != ESP_OK) {
        LOG_E(TAG, "Pulse counter initialization failed");
        return ERROR_RESOURCE;
    }

    pcnt_glitch_filter_config_t filter_config = { .max_glitch_ns = 1000 };
    if (pcnt_unit_set_glitch_filter(unit, &filter_config) != ESP_OK) {
        LOG_E(TAG, "Pulse counter glitch filter config failed");
        pcnt_del_unit(unit);
        return ERROR_RESOURCE;
    }

    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = pin_b,
        .level_gpio_num = pin_a,
        .flags = {},
    };
    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num = pin_a,
        .level_gpio_num = pin_b,
        .flags = {},
    };

    pcnt_channel_handle_t chan_a = nullptr;
    pcnt_channel_handle_t chan_b = nullptr;
    if (pcnt_new_channel(unit, &chan_a_config, &chan_a) != ESP_OK ||
        pcnt_new_channel(unit, &chan_b_config, &chan_b) != ESP_OK) {
        LOG_E(TAG, "Pulse counter channel config failed");
        pcnt_del_unit(unit);
        return ERROR_RESOURCE;
    }

    // Standard quadrature decode: each channel counts on its edge, direction decided by the
    // other channel's level.
    if (pcnt_channel_set_edge_action(chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE) != ESP_OK ||
        pcnt_channel_set_edge_action(chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE) != ESP_OK) {
        LOG_E(TAG, "Pulse counter edge action config failed");
        pcnt_del_unit(unit);
        return ERROR_RESOURCE;
    }
    if (pcnt_channel_set_level_action(chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE) != ESP_OK ||
        pcnt_channel_set_level_action(chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE) != ESP_OK) {
        LOG_E(TAG, "Pulse counter level action config failed");
        pcnt_del_unit(unit);
        return ERROR_RESOURCE;
    }

    if (pcnt_unit_add_watch_point(unit, PCNT_LOW_LIMIT) != ESP_OK ||
        pcnt_unit_add_watch_point(unit, PCNT_HIGH_LIMIT) != ESP_OK) {
        LOG_E(TAG, "Pulse counter watch point config failed");
        pcnt_del_unit(unit);
        return ERROR_RESOURCE;
    }

    if (pcnt_unit_enable(unit) != ESP_OK ||
        pcnt_unit_clear_count(unit) != ESP_OK ||
        pcnt_unit_start(unit) != ESP_OK) {
        LOG_E(TAG, "Pulse counter could not be started");
        pcnt_del_unit(unit);
        return ERROR_RESOURCE;
    }

    *out_unit = unit;
    return ERROR_NONE;
}

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);

    // Backstop for values the devicetree compiler doesn't currently validate: 0 divides by
    // zero in poll_wheel(), and a capacity below 2 can never hold one press/release pair.
    if (config->pulses_per_detent == 0) {
        LOG_E(TAG, "pulses_per_detent must be > 0");
        return ERROR_INVALID_ARGUMENT;
    }
    if (config->pending_capacity < 2) {
        LOG_E(TAG, "pending_capacity must be >= 2");
        return ERROR_INVALID_ARGUMENT;
    }

    auto* internal = new (std::nothrow) GpioEncoderInternal();
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    internal->pulses_per_detent = static_cast<int32_t>(config->pulses_per_detent);
    internal->pending_capacity = config->pending_capacity;

    internal->pending = new (std::nothrow) GpioEncoderPendingEvent[internal->pending_capacity];
    if (internal->pending == nullptr) {
        delete internal;
        return ERROR_OUT_OF_MEMORY;
    }

    internal->pin_a = gpio_descriptor_acquire(config->pin_a.gpio_controller, config->pin_a.pin, config->pin_a.flags | GPIO_FLAG_DIRECTION_INPUT, GPIO_OWNER_GPIO);
    if (internal->pin_a == nullptr) {
        LOG_E(TAG, "Failed to acquire pin_a");
        delete[] internal->pending;
        delete internal;
        return ERROR_RESOURCE;
    }

    internal->pin_b = gpio_descriptor_acquire(config->pin_b.gpio_controller, config->pin_b.pin, config->pin_b.flags | GPIO_FLAG_DIRECTION_INPUT, GPIO_OWNER_GPIO);
    if (internal->pin_b == nullptr) {
        LOG_E(TAG, "Failed to acquire pin_b");
        gpio_descriptor_release(internal->pin_a);
        delete[] internal->pending;
        delete internal;
        return ERROR_RESOURCE;
    }

    int native_pin_a = 0;
    int native_pin_b = 0;
    if (gpio_descriptor_get_native_pin_number(internal->pin_a, &native_pin_a) != ERROR_NONE ||
        gpio_descriptor_get_native_pin_number(internal->pin_b, &native_pin_b) != ERROR_NONE) {
        LOG_E(TAG, "Failed to resolve native pin numbers");
        gpio_descriptor_release(internal->pin_b);
        gpio_descriptor_release(internal->pin_a);
        delete[] internal->pending;
        delete internal;
        return ERROR_RESOURCE;
    }

    error_t error = init_pcnt_unit(native_pin_a, native_pin_b, &internal->pcnt_unit);
    if (error != ERROR_NONE) {
        gpio_descriptor_release(internal->pin_b);
        gpio_descriptor_release(internal->pin_a);
        delete[] internal->pending;
        delete internal;
        return error;
    }

    if (config->pin_enter.gpio_controller != nullptr) {
        internal->pin_enter = gpio_descriptor_acquire(config->pin_enter.gpio_controller, config->pin_enter.pin, GPIO_FLAG_DIRECTION_INPUT | GPIO_FLAG_ACTIVE_LOW, GPIO_OWNER_GPIO);
        if (internal->pin_enter == nullptr) {
            pcnt_unit_stop(internal->pcnt_unit);
            pcnt_del_unit(internal->pcnt_unit);
            gpio_descriptor_release(internal->pin_b);
            gpio_descriptor_release(internal->pin_a);
            delete[] internal->pending;
            delete internal;
            return ERROR_RESOURCE;
        }
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = GET_INTERNAL(device);

    if (internal->pin_enter != nullptr) {
        gpio_descriptor_release(internal->pin_enter);
    }

    if (pcnt_unit_stop(internal->pcnt_unit) != ESP_OK) {
        LOG_W(TAG, "Failed to stop encoder");
    }
    if (pcnt_del_unit(internal->pcnt_unit) != ESP_OK) {
        LOG_W(TAG, "Failed to delete encoder");
    }

    gpio_descriptor_release(internal->pin_b);
    gpio_descriptor_release(internal->pin_a);

    device_set_driver_data(device, nullptr);
    delete[] internal->pending;
    delete internal;
    return ERROR_NONE;
}

// endregion

// region KeyboardApi

// Wheel rotation is a discrete notch, not a held key, so each detent is reported as an
// immediate press+release pair rather than a persistent pressed state.
static void poll_wheel(GpioEncoderInternal* internal) {
    int pulses = 0;
    pcnt_unit_get_count(internal->pcnt_unit, &pulses);
    pcnt_unit_clear_count(internal->pcnt_unit);

    int32_t total = internal->pulse_remainder + pulses;
    int32_t detents = total / internal->pulses_per_detent;
    internal->pulse_remainder = total % internal->pulses_per_detent;

    uint32_t key = detents >= 0 ? CODEPOINT_FOCUS_NEXT : CODEPOINT_FOCUS_PREVIOUS;
    int32_t count = detents >= 0 ? detents : -detents;
    for (int32_t i = 0; i < count; i++) {
        // A press without its matching release would leave the consumer thinking the key
        // is stuck down, so only enqueue the pair when both fit.
        if (internal->pending_count + 2 > internal->pending_capacity) {
            LOG_W(TAG, "Pending event queue full, dropping remaining wheel events");
            break;
        }
        push_pending(internal, key, true);
        push_pending(internal, key, false);
    }
}

static void poll_button(GpioEncoderInternal* internal) {
    if (internal->pin_enter == nullptr) {
        return;
    }

    bool pressed = false;
    if (gpio_descriptor_get_level(internal->pin_enter, &pressed) != ERROR_NONE) {
        return;
    }
    // Only commit the new state once its event is actually queued - a full FIFO here
    // leaves button_pressed unchanged so the same transition is retried next poll instead
    // of being lost.
    if (pressed != internal->button_pressed) {
        if (push_pending(internal, CODEPOINT_ENTER, pressed)) {
            internal->button_pressed = pressed;
        }
    }
}

static error_t gpio_encoder_read_key(Device* device, KeyboardKeyData* data) {
    auto* internal = GET_INTERNAL(device);

    poll_wheel(internal);
    poll_button(internal);

    GpioEncoderPendingEvent event;
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

static constexpr KeyboardApi GPIO_ENCODER_API = {
    .read_key = gpio_encoder_read_key,
};

extern Module gpio_encoder_module;

Driver gpio_encoder_driver = {
    .name = "gpio_encoder",
    .compatible = (const char*[]) { "tactility,gpio-encoder", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &GPIO_ENCODER_API,
    .device_type = &KEYBOARD_TYPE,
    .owner = &gpio_encoder_module,
    .internal = nullptr
};

}
