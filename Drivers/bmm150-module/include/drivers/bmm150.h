// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>

#include <tactility/drivers/compass.h>
#include <tactility/error.h>

struct Device;

#ifdef __cplusplus
extern "C" {
#endif

struct Bmm150Config {
    /** I2C address on the bus */
    uint8_t address;
    /**
     * Row-major 3x3 rotation matrix mapping the sensor frame onto the device frame (Linux IIO
     * "mount-matrix" convention: `out[i] = m[i * 3] * x + m[i * 3 + 1] * y + m[i * 3 + 2] * z`).
     */
    int32_t mount_matrix[9];
};

/** Read the compensated magnetic field in µT, in the device frame. */
error_t bmm150_read_field(struct Device* device, struct CompassData* data);

#ifdef __cplusplus
}
#endif
