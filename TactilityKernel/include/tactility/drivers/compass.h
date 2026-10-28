// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/device.h>
#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

struct CompassData {
    float x, y, z; // magnetic field strength, in µT
};

/**
 * @brief API for compass (magnetometer) drivers.
 *
 * @note read_field() is required. read_heading() is optional: a driver that can't service it
 * leaves it NULL, and compass_read_heading() derives the heading from the x and y axes.
 */
struct CompassApi {
    /**
     * @brief Reads the current magnetic field.
     * @param[in] device the compass device
     * @param[out] data pointer to store the current reading
     * @retval ERROR_NONE on success
     */
    error_t (*read_field)(struct Device* device, struct CompassData* data);

    /**
     * @brief Reads the current heading.
     * @param[in] device the compass device
     * @param[out] heading_deg pointer to store the heading, in degrees clockwise from magnetic north [0, 360)
     * @retval ERROR_NONE on success
     */
    error_t (*read_heading)(struct Device* device, float* heading_deg);
};

/**
 * @brief Reads the current magnetic field using the specified compass device, in µT.
 */
error_t compass_read_field(struct Device* device, struct CompassData* data);

/**
 * @brief Reads the current heading using the specified compass device.
 * Assumes the device is held level when the driver has no heading of its own.
 * @param[out] heading_deg the heading in degrees clockwise from magnetic north [0, 360)
 */
error_t compass_read_heading(struct Device* device, float* heading_deg);

extern const struct DeviceType COMPASS_TYPE;

#ifdef __cplusplus
}
#endif
