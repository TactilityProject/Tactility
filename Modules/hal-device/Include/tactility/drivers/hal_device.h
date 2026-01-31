// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

enum class HalDeviceType {
    HAL_DEVICE_TYPE_I2C,
    HAL_DEVICE_TYPE_DISPLAY,
    HAL_DEVICE_TYPE_TOUCH,
    HAL_DEVICE_TYPE_SDCARD,
    HAL_DEVICE_TYPE_KEYBOARD,
    HAL_DEVICE_TYPE_ENCODER,
    HAL_DEVICE_TYPE_POWER,
    HAL_DEVICE_TYPE_GPS,
    HAL_DEVICE_TYPE_OTHER
};

HalDeviceType hal_device_get_type(struct Device* device);

void hal_device_for_each_of_type(HalDeviceType type, void* context, bool(*onDevice)(struct Device* device, void* context));

extern const struct DeviceType HAL_DEVICE_TYPE;

#ifdef __cplusplus
}
#endif
