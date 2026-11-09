// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>
#include <tactility/error.h>

struct Device;

#ifdef __cplusplus
extern "C" {
#endif

struct Shtc3Config {
    /** Address on bus */
    uint8_t address;
};

/**
 * Measure the temperature and the relative humidity.
 * The output pointers must not be null.
 * @param[in]  device             shtc3 device
 * @param[out] celsius            temperature in degrees Celsius
 * @param[out] humidity_percent   relative humidity in percent
 * @return ERROR_NONE on success, ERROR_INVALID_STATE when the data failed its checksum
 */
error_t shtc3_read(struct Device* device, float* celsius, float* humidity_percent);

#ifdef __cplusplus
}
#endif
