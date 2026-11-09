// SPDX-License-Identifier: Apache-2.0
#include <drivers/pwm_vibration.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/pwm.h>
#include <tactility/drivers/vibration.h>
#include <tactility/log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <atomic>
#include <new>

#define TAG "PwmVibration"
#define GET_CONFIG(device) (static_cast<const PwmVibrationConfig*>((device)->config))
#define GET_INTERNAL(device) (static_cast<PwmVibrationInternal*>(device_get_driver_data(device)))

namespace {

struct PulseRequest {
    uint32_t duration_ms;
    float percent;
    uint32_t generation;
};

struct PwmVibrationInternal {
    QueueHandle_t queue = nullptr;
    TaskHandle_t task = nullptr;
    /** Incremented by every pulse/strength request so a stale pulse does not clear a newer one. */
    std::atomic<uint32_t> generation { 0 };
};

error_t apply_strength(Device* device, float percent) {
    const auto* config = GET_CONFIG(device);
    if (config->pwm == nullptr) {
        return ERROR_INVALID_STATE;
    }

    if (percent <= 0.0f) {
        pwm_set_duty(config->pwm, 0);
        return pwm_disable(config->pwm);
    }
    if (percent > 100.0f) {
        percent = 100.0f;
    }

    uint32_t period_ns = 0;
    error_t error = pwm_get_period(config->pwm, &period_ns);
    if (error != ERROR_NONE) {
        return error;
    }

    const uint32_t duty_ns = static_cast<uint32_t>(
        (static_cast<uint64_t>(period_ns) * static_cast<uint32_t>(percent * 10.0f)) / 1000u
    );
    error = pwm_set_duty(config->pwm, duty_ns);
    if (error != ERROR_NONE) {
        return error;
    }
    return pwm_enable(config->pwm);
}

void pulse_task(void* arg) {
    auto* device = static_cast<Device*>(arg);
    PulseRequest request = {};
    while (true) {
        if (xQueueReceive(GET_INTERNAL(device)->queue, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        // A newer pulse or set_strength() superseded this request before it was applied. Skip it
        // so it cannot switch the motor on after the newer request already took effect.
        if (GET_INTERNAL(device)->generation.load() != request.generation) {
            continue;
        }
        apply_strength(device, request.percent);
        vTaskDelay(pdMS_TO_TICKS(request.duration_ms));
        // A newer pulse or a sustained set_strength() supersedes this pulse. Only switch the
        // motor off when this request is still the active one.
        if (GET_INTERNAL(device)->generation.load() == request.generation) {
            apply_strength(device, 0.0f);
        }
    }
}

// region Driver lifecycle

error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);
    if (config->pwm == nullptr) {
        LOG_E(TAG, "No PWM device configured");
        return ERROR_INVALID_STATE;
    }

    auto* internal = new(std::nothrow) PwmVibrationInternal();
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    internal->queue = xQueueCreate(1, sizeof(PulseRequest));
    if (internal->queue == nullptr) {
        delete internal;
        return ERROR_OUT_OF_MEMORY;
    }

    // Set the driver data before starting the task. The task looks it up immediately after
    // creation, so starting it first can run the (higher-priority) task against a null pointer.
    device_set_driver_data(device, internal);

    if (xTaskCreate(&pulse_task, "pwm_vibration", 3072, device, 3, &internal->task) != pdPASS) {
        device_set_driver_data(device, nullptr);
        vQueueDelete(internal->queue);
        delete internal;
        return ERROR_OUT_OF_MEMORY;
    }

    apply_strength(device, 0.0f);
    return ERROR_NONE;
}

error_t stop(Device* device) {
    auto* internal = GET_INTERNAL(device);
    if (internal != nullptr) {
        // Publish the teardown before freeing anything so API calls that start now see no driver
        // state. stop_device() is a teardown path, so no API call should be in flight.
        device_set_driver_data(device, nullptr);
        if (internal->task != nullptr) {
            vTaskDelete(internal->task);
            internal->task = nullptr;
        }
        if (internal->queue != nullptr) {
            vQueueDelete(internal->queue);
            internal->queue = nullptr;
        }
        apply_strength(device, 0.0f);
        delete internal;
    }
    return ERROR_NONE;
}

// endregion

// region VibrationApi

error_t set_strength(Device* device, float percent) {
    auto* internal = GET_INTERNAL(device);
    if (internal != nullptr) {
        // Cancel any in-flight pulse so it cannot clear this sustained strength afterwards.
        internal->generation.fetch_add(1);
    }
    return apply_strength(device, percent);
}

error_t pulse(Device* device, uint32_t duration_ms, float percent) {
    auto* internal = GET_INTERNAL(device);
    if (internal == nullptr || internal->queue == nullptr) {
        return ERROR_INVALID_STATE;
    }
    const uint32_t generation = internal->generation.fetch_add(1) + 1;
    const PulseRequest request = { .duration_ms = duration_ms, .percent = percent, .generation = generation };
    if (xQueueOverwrite(internal->queue, &request) != pdTRUE) {
        return ERROR_RESOURCE;
    }
    return ERROR_NONE;
}

// endregion

const VibrationApi PWM_VIBRATION_API = {
    .set_strength = set_strength,
    .pulse = pulse,
};

} // namespace

extern "C" {

Driver pwm_vibration_driver = {
    .name = "pwm_vibration",
    .compatible = (const char*[]) { "tactility,pwm-vibration", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = &PWM_VIBRATION_API,
    .device_type = &VIBRATION_TYPE,
    .internal = nullptr
};

}
