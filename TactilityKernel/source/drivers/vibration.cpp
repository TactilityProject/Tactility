// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/vibration.h>

#include <tactility/device.h>

#define VIBRATION_DRIVER_API(device) ((struct VibrationApi*)device_get_driver(device)->api)

extern "C" {

error_t vibration_set_strength(Device* device, float percent) {
    const auto* api = VIBRATION_DRIVER_API(device);
    if (api->set_strength == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    return api->set_strength(device, percent);
}

error_t vibration_pulse(Device* device, uint32_t duration_ms, float percent) {
    const auto* api = VIBRATION_DRIVER_API(device);
    if (api->pulse == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    return api->pulse(device, duration_ms, percent);
}

const DeviceType VIBRATION_TYPE {
    .name = "vibration"
};

}
