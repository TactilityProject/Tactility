// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>
#include <tactility/error.h>

struct Device;

#ifdef __cplusplus
extern "C" {
#endif

struct Qmi8658Config {
    /** I2C address (0x6A when SA0=low, 0x6B when SA0=high) */
    uint8_t address;
};

struct Qmi8658Data {
    float ax, ay, az; // acceleration in g (±8g range)
    float gx, gy, gz; // angular rate in °/s (±2048°/s range)
};

/**
 * Read accelerometer and gyroscope data.
 * @param[in]  device qmi8658 device
 * @param[out] data   Pointer to Qmi8658Data to populate
 * @return ERROR_NONE on success
 */
error_t qmi8658_read(struct Device* device, struct Qmi8658Data* data);

#ifdef __cplusplus
}
#endif
