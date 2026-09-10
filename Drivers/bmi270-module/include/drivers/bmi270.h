// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>
#include <tactility/drivers/imu.h>
#include <tactility/error.h>

struct Device;

#ifdef __cplusplus
extern "C" {
#endif

struct Bmi270Config {
    /** Address on bus */
    uint8_t address;
};

/**
 * Read accelerometer data only.
 * @param[in] device bmi270 device
 * @param[out] data Pointer to ImuAccelData structure to store the data (±8g range)
 * @return ERROR_NONE on success
 */
error_t bmi270_read_accel(struct Device* device, struct ImuAccelData* data);

/**
 * Read gyroscope data only.
 * @param[in] device bmi270 device
 * @param[out] data Pointer to ImuGyroData structure to store the data (±2000°/s range)
 * @return ERROR_NONE on success
 */
error_t bmi270_read_gyro(struct Device* device, struct ImuGyroData* data);

/**
 * Read the chip's on-die temperature.
 * @param[in] device bmi270 device
 * @param[out] temperature_c Pointer to store the temperature, in °C
 * @return ERROR_NONE on success
 */
error_t bmi270_read_temperature(struct Device* device, float* temperature_c);

#ifdef __cplusplus
}
#endif
