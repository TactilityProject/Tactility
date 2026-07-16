// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/esp32_pwm_ledc.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/pwm.h>
#include <tactility/log.h>

#include <driver/ledc.h>
#include <esp_err.h>

#include <cstdlib>

#define TAG "Esp32PwmLedc"
#define GET_CONFIG(device) (static_cast<const Esp32PwmLedcConfig*>((device)->config))
#define GET_INTERNAL(device) (static_cast<Esp32PwmLedcInternal*>(device_get_driver_data(device)))

struct Esp32PwmLedcInternal {
    uint32_t period_ns;
    uint32_t duty_ns;
    bool inverted;
    bool enabled;
};

// region Helpers

static uint32_t compute_freq_hz(uint32_t period_ns) {
    return period_ns > 0 ? (uint32_t)(1000000000ULL / period_ns) : 0;
}

static uint32_t compute_raw_duty(uint32_t duty_ns, uint32_t period_ns, ledc_timer_bit_t duty_resolution) {
    if (period_ns == 0) return 0;
    uint64_t max_duty = 1ULL << duty_resolution;
    uint64_t raw_duty = ((uint64_t)duty_ns * max_duty) / period_ns;
    return (uint32_t)(raw_duty > max_duty ? max_duty : raw_duty);
}

// Reprograms the LEDC timer's frequency/resolution. Independent of the enabled state: it doesn't
// touch the channel's signal-output-enable bit, so it's safe to call while output is stopped.
static error_t apply_period(Device* device) {
    const auto* config = GET_CONFIG(device);
    const auto* internal = GET_INTERNAL(device);

    ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = config->duty_resolution,
        .timer_num = config->ledc_timer,
        .freq_hz = compute_freq_hz(internal->period_ns),
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false,
    };
    if (ledc_timer_config(&timer_config) != ESP_OK) {
        LOG_E(TAG, "Failed to configure LEDC timer");
        return ERROR_RESOURCE;
    }
    return ERROR_NONE;
}

// ledc_update_duty() unconditionally re-enables the channel's signal output, so this only
// touches hardware while the device is enabled; a pending duty/period change made while disabled
// is picked up from internal state the next time enable() is called.
static error_t apply_duty(Device* device) {
    const auto* config = GET_CONFIG(device);
    const auto* internal = GET_INTERNAL(device);
    if (!internal->enabled) {
        return ERROR_NONE;
    }

    uint32_t raw_duty = compute_raw_duty(internal->duty_ns, internal->period_ns, config->duty_resolution);
    esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, config->ledc_channel, raw_duty);
    if (ret == ESP_OK) {
        ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, config->ledc_channel);
    }
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to set duty: %s", esp_err_to_name(ret));
        return ERROR_RESOURCE;
    }
    return ERROR_NONE;
}

// Rebuilds the LEDC channel (duty, output polarity, timer/pin binding) from current internal
// state. Like ledc_update_duty(), this unconditionally re-enables the channel's signal output,
// so callers must only invoke this while the device is meant to be enabled.
static error_t apply_channel(Device* device) {
    const auto* config = GET_CONFIG(device);
    const auto* internal = GET_INTERNAL(device);

    ledc_channel_config_t channel_config = {
        .gpio_num = (int)config->pin.pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = config->ledc_channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = config->ledc_timer,
        .duty = compute_raw_duty(internal->duty_ns, internal->period_ns, config->duty_resolution),
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags = {
            .output_invert = internal->inverted ? 1u : 0u,
        },
    };
    if (ledc_channel_config(&channel_config) != ESP_OK) {
        LOG_E(TAG, "Failed to configure LEDC channel");
        return ERROR_RESOURCE;
    }
    return ERROR_NONE;
}

// endregion

// region Driver lifecycle

// Nothing here touches LEDC hardware: period/duty/inverted may be overridden via the PwmApi
// before the first enable() call, so construction only needs to seed tracked state from config.
// enable() is what actually programs the timer and channel from that tracked state.
static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);

    auto* internal = static_cast<Esp32PwmLedcInternal*>(malloc(sizeof(Esp32PwmLedcInternal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    internal->period_ns = config->period_ns;
    internal->duty_ns = config->duty_ns;
    internal->inverted = config->inverted;
    internal->enabled = false;

    device_set_driver_data(device, internal);

    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = GET_INTERNAL(device);
    if (internal->enabled) {
        const auto* config = GET_CONFIG(device);
        ledc_stop(LEDC_LOW_SPEED_MODE, config->ledc_channel, 0); // Allowed to fail, we don't care about the result
    }

    device_set_driver_data(device, nullptr);
    free(internal);

    return ERROR_NONE;
}

// endregion

// region PwmApi

static error_t esp32_pwm_ledc_set_period(Device* device, uint32_t period_ns) {
    GET_INTERNAL(device)->period_ns = period_ns;
    error_t error = apply_period(device);
    if (error != ERROR_NONE) {
        return error;
    }
    return apply_duty(device);
}

static error_t esp32_pwm_ledc_get_period(Device* device, uint32_t* period_ns) {
    *period_ns = GET_INTERNAL(device)->period_ns;
    return ERROR_NONE;
}

static error_t esp32_pwm_ledc_set_duty(Device* device, uint32_t duty_ns) {
    GET_INTERNAL(device)->duty_ns = duty_ns;
    return apply_duty(device);
}

static error_t esp32_pwm_ledc_get_duty(Device* device, uint32_t* duty_ns) {
    *duty_ns = GET_INTERNAL(device)->duty_ns;
    return ERROR_NONE;
}

static error_t esp32_pwm_ledc_set_inverted(Device* device, bool inverted) {
    auto* internal = GET_INTERNAL(device);
    internal->inverted = inverted;

    // While disabled, just track the override; apply_channel() rebuilds the channel with it
    // (and every other tracked setting) the next time enable() runs.
    if (!internal->enabled) {
        return ERROR_NONE;
    }
    return apply_channel(device);
}

static error_t esp32_pwm_ledc_is_inverted(Device* device, bool* inverted) {
    *inverted = GET_INTERNAL(device)->inverted;
    return ERROR_NONE;
}

// Applies the tracked period, duty and inverted settings (whether they came from the config
// defaults or were overridden beforehand) and turns the output on.
static error_t esp32_pwm_ledc_enable(Device* device) {
    error_t error = apply_period(device);
    if (error != ERROR_NONE) {
        return error;
    }

    error = apply_channel(device);
    if (error != ERROR_NONE) {
        return error;
    }

    GET_INTERNAL(device)->enabled = true;
    return ERROR_NONE;
}

static error_t esp32_pwm_ledc_disable(Device* device) {
    auto* internal = GET_INTERNAL(device);
    if (!internal->enabled) {
        return ERROR_NONE;
    }

    const auto* config = GET_CONFIG(device);
    internal->enabled = false;

    if (ledc_stop(LEDC_LOW_SPEED_MODE, config->ledc_channel, 0) != ESP_OK) {
        LOG_E(TAG, "Failed to stop LEDC channel");
        return ERROR_RESOURCE;
    }
    return ERROR_NONE;
}

static error_t esp32_pwm_ledc_is_enabled(Device* device, bool* enabled) {
    *enabled = GET_INTERNAL(device)->enabled;
    return ERROR_NONE;
}

// endregion

static const PwmApi esp32_pwm_ledc_api = {
    .set_period = esp32_pwm_ledc_set_period,
    .get_period = esp32_pwm_ledc_get_period,
    .set_duty = esp32_pwm_ledc_set_duty,
    .get_duty = esp32_pwm_ledc_get_duty,
    .set_inverted = esp32_pwm_ledc_set_inverted,
    .is_inverted = esp32_pwm_ledc_is_inverted,
    .enable = esp32_pwm_ledc_enable,
    .disable = esp32_pwm_ledc_disable,
    .is_enabled = esp32_pwm_ledc_is_enabled,
};

extern Module platform_esp32_module;

Driver esp32_pwm_ledc_driver = {
    .name = "esp32_pwm_ledc",
    .compatible = (const char*[]) { "espressif,esp32-pwm-ledc", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &esp32_pwm_ledc_api,
    .device_type = &PWM_TYPE,
    .owner = &platform_esp32_module,
    .internal = nullptr
};
