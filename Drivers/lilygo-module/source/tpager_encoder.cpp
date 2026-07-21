// SPDX-License-Identifier: Apache-2.0
#include <lilygo/drivers/tpager_encoder.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/gpio_descriptor.h>
#include <tactility/log.h>

#include <driver/pulse_cnt.h>

#include <new>

#define TAG "tpager_encoder"
#define GET_CONFIG(device) (static_cast<const TpagerEncoderConfig*>((device)->config))
#define GET_INTERNAL(device) (static_cast<TpagerEncoderInternal*>(device_get_driver_data(device)))

struct TpagerEncoderInternal {
    pcnt_unit_handle_t pcnt_unit = nullptr;
    GpioDescriptor* pin_enter = nullptr;
};

extern "C" {

static error_t read_delta(Device* device, int32_t* out_pulses) {
    auto* internal = GET_INTERNAL(device);
    int pulses = 0;
    pcnt_unit_get_count(internal->pcnt_unit, &pulses);
    pcnt_unit_clear_count(internal->pcnt_unit);
    *out_pulses = pulses;
    return ERROR_NONE;
}

static error_t get_button_pressed(Device* device, bool* out_pressed) {
    auto* internal = GET_INTERNAL(device);
    return gpio_descriptor_get_level(internal->pin_enter, out_pressed);
}

error_t tpager_encoder_read_delta(Device* device, int32_t* out_pulses) {
    return read_delta(device, out_pulses);
}

error_t tpager_encoder_get_button_pressed(Device* device, bool* out_pressed) {
    return get_button_pressed(device, out_pressed);
}

// region Driver lifecycle

// Accumulating count makes over-/underflow automatically compensated; requires watch points at
// the low and high limits (see pcnt_unit_add_watch_point() below). Ported from the deprecated
// HAL's TpagerEncoder::initEncoder().
static constexpr int PCNT_LOW_LIMIT = -127;
static constexpr int PCNT_HIGH_LIMIT = 126;

static error_t init_pcnt_unit(const TpagerEncoderConfig* config, pcnt_unit_handle_t* out_unit) {
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
        .edge_gpio_num = static_cast<int>(config->pin_b.pin),
        .level_gpio_num = static_cast<int>(config->pin_a.pin),
        .flags = {},
    };
    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num = static_cast<int>(config->pin_a.pin),
        .level_gpio_num = static_cast<int>(config->pin_b.pin),
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

    auto* internal = new (std::nothrow) TpagerEncoderInternal();
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    error_t error = init_pcnt_unit(config, &internal->pcnt_unit);
    if (error != ERROR_NONE) {
        delete internal;
        return error;
    }

    internal->pin_enter = gpio_descriptor_acquire(config->pin_enter.gpio_controller, config->pin_enter.pin, GPIO_FLAG_DIRECTION_INPUT | GPIO_FLAG_ACTIVE_LOW, GPIO_OWNER_GPIO);
    if (internal->pin_enter == nullptr) {
        pcnt_unit_stop(internal->pcnt_unit);
        pcnt_del_unit(internal->pcnt_unit);
        delete internal;
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = GET_INTERNAL(device);

    gpio_descriptor_release(internal->pin_enter);

    if (pcnt_unit_stop(internal->pcnt_unit) != ESP_OK) {
        LOG_W(TAG, "Failed to stop encoder");
    }
    if (pcnt_del_unit(internal->pcnt_unit) != ESP_OK) {
        LOG_W(TAG, "Failed to delete encoder");
    }

    device_set_driver_data(device, nullptr);
    delete internal;
    return ERROR_NONE;
}

// endregion

static constexpr TpagerEncoderApi TPAGER_ENCODER_API = {
    .read_delta = read_delta,
    .get_button_pressed = get_button_pressed,
};

const struct DeviceType TPAGER_ENCODER_TYPE {
    .name = "tpager-encoder"
};

extern Module lilygo_module;

Driver tpager_encoder_driver = {
    .name = "tpager_encoder",
    .compatible = (const char*[]) { "lilygo,tpager-encoder", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &TPAGER_ENCODER_API,
    .device_type = &TPAGER_ENCODER_TYPE,
    .owner = &lilygo_module,
    .internal = nullptr
};

}
