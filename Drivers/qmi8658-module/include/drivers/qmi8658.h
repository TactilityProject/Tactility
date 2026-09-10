// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>
#include <tactility/drivers/imu.h>
#include <tactility/error.h>

struct Device;

#ifdef __cplusplus
extern "C" {
#endif

struct Qmi8658Config {
    /** I2C address (0x6A when SA0=low, 0x6B when SA0=high) */
    uint8_t address;
};

/**
 * Read accelerometer and gyroscope data.
 * @param[in]  device qmi8658 device
 * @param[out] data   Pointer to ImuData to populate (±8g / ±2048°/s range)
 * @return ERROR_NONE on success
 */
error_t qmi8658_read(struct Device* device, struct ImuData* data);

/**
 * Read accelerometer data only.
 * @param[in]  device qmi8658 device
 * @param[out] data   Pointer to ImuAccelData to populate (±8g range)
 * @return ERROR_NONE on success
 */
error_t qmi8658_read_accel(struct Device* device, struct ImuAccelData* data);

/**
 * Read gyroscope data only.
 * @param[in]  device qmi8658 device
 * @param[out] data   Pointer to ImuGyroData to populate (±2048°/s range)
 * @return ERROR_NONE on success
 */
error_t qmi8658_read_gyro(struct Device* device, struct ImuGyroData* data);

/**
 * Read the chip's on-die temperature.
 * @param[in]  device qmi8658 device
 * @param[out] temperature_c Pointer to store the temperature, in °C
 * @return ERROR_NONE on success
 */
error_t qmi8658_read_temperature(struct Device* device, float* temperature_c);

#ifdef __cplusplus
}
#endif
