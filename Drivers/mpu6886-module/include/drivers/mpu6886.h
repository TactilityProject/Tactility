// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>
#include <tactility/error.h>

struct Device;

#ifdef __cplusplus
extern "C" {
#endif

struct Mpu6886Config {
    /** Address on bus */
    uint8_t address;
};

struct Mpu6886Data {
    float ax, ay, az; // acceleration in g (±8g range)
    float gx, gy, gz; // angular rate in °/s (±2000°/s range)
};

/**
 * Read accelerometer and gyroscope data.
 * @param[in]  device mpu6886 device
 * @param[out] data   Pointer to Mpu6886Data to populate
 * @return ERROR_NONE on success
 */
error_t mpu6886_read(struct Device* device, struct Mpu6886Data* data);

#ifdef __cplusplus
}
#endif
