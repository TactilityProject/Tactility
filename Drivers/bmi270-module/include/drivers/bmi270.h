// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>
#include <tactility/error.h>

struct Device;

#ifdef __cplusplus
extern "C" {
#endif

struct Bmi270Config {
    /** Address on bus */
    uint8_t address;
};

struct Bmi270Data {
    float ax, ay, az; // acceleration in g (±8g range)
    float gx, gy, gz; // angular rate in °/s (±2000°/s range)
};

/**
 * Read accelerometer and gyroscope data.
 * @param[in] device bmi270 device
 * @param[out] data Pointer to Bmi270Data structure to store the data
 * @return ERROR_NONE on success
 */
error_t bmi270_read(struct Device* device, struct Bmi270Data* data);

#ifdef __cplusplus
}
#endif
