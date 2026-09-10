// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/imu.h>
#include <tactility/device.h>

#define IMU_DRIVER_API(driver) ((struct ImuApi*)driver->api)

extern "C" {

error_t imu_read(Device* device, ImuData* data) {
    const auto* driver = device_get_driver(device);
    return IMU_DRIVER_API(driver)->read(device, data);
}

error_t imu_read_accel(Device* device, ImuAccelData* data) {
    const auto* driver = device_get_driver(device);
    auto* api = IMU_DRIVER_API(driver);
    if (!api->read_accel) return ERROR_NOT_SUPPORTED;
    return api->read_accel(device, data);
}

error_t imu_read_gyro(Device* device, ImuGyroData* data) {
    const auto* driver = device_get_driver(device);
    auto* api = IMU_DRIVER_API(driver);
    if (!api->read_gyro) return ERROR_NOT_SUPPORTED;
    return api->read_gyro(device, data);
}

error_t imu_read_temperature(Device* device, float* temperature_c) {
    const auto* driver = device_get_driver(device);
    auto* api = IMU_DRIVER_API(driver);
    if (!api->read_temperature) return ERROR_NOT_SUPPORTED;
    return api->read_temperature(device, temperature_c);
}

const DeviceType IMU_TYPE {
    .name = "imu"
};

}
