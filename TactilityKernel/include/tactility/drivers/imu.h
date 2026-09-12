// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/device.h>
#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ImuAccelData {
    float ax, ay, az; // acceleration, in g
};

struct ImuGyroData {
    float gx, gy, gz; // angular rate, in °/s
};

/**
 * @brief API for IMU (accelerometer + gyroscope) drivers.
 *
 * @note read_accel(), read_gyro() and read_temperature() are each a single-register-window I2C
 * transaction. All three are optional per driver: a driver that can't service one leaves it
 * NULL, and the matching imu_read_*() wrapper returns ERROR_NOT_SUPPORTED.
 */
struct ImuApi {
    /**
     * @brief Reads only the current accelerometer data.
     * @param[in] device the IMU device
     * @param[out] data pointer to store the current reading
     * @retval ERROR_NOT_SUPPORTED if the driver has no accel-only read path
     * @retval ERROR_NONE on success
     */
    error_t (*read_accel)(struct Device* device, struct ImuAccelData* data);

    /**
     * @brief Reads only the current gyroscope data.
     * @param[in] device the IMU device
     * @param[out] data pointer to store the current reading
     * @retval ERROR_NOT_SUPPORTED if the driver has no gyro-only read path
     * @retval ERROR_NONE on success
     */
    error_t (*read_gyro)(struct Device* device, struct ImuGyroData* data);

    /**
     * @brief Reads the chip's on-die temperature.
     * @param[in] device the IMU device
     * @param[out] temperature_c pointer to store the current reading, in °C
     * @retval ERROR_NOT_SUPPORTED if the driver has no temperature sensor/register
     * @retval ERROR_NONE on success
     */
    error_t (*read_temperature)(struct Device* device, float* temperature_c);
};

/**
 * @brief Reads only the current accelerometer data using the specified IMU device.
 * @retval ERROR_NOT_SUPPORTED if the driver has no accel-only read path
 */
error_t imu_read_accel(struct Device* device, struct ImuAccelData* data);

/**
 * @brief Reads only the current gyroscope data using the specified IMU device.
 * @retval ERROR_NOT_SUPPORTED if the driver has no gyro-only read path
 */
error_t imu_read_gyro(struct Device* device, struct ImuGyroData* data);

/**
 * @brief Reads the chip's on-die temperature using the specified IMU device, in °C.
 * @retval ERROR_NOT_SUPPORTED if the driver has no temperature sensor/register
 */
error_t imu_read_temperature(struct Device* device, float* temperature_c);

extern const struct DeviceType IMU_TYPE;

#ifdef __cplusplus
}
#endif
