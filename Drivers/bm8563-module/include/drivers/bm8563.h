// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>
#include <tactility/drivers/rtc.h>
#include <tactility/error.h>

struct Device;

#ifdef __cplusplus
extern "C" {
#endif

struct Bm8563Config {
    /** Address on bus */
    uint8_t address;
};

/**
 * Read the current date and time from the RTC.
 * @param[in]  device bm8563 device
 * @param[out] dt     Pointer to RtcDateTime to populate
 * @return ERROR_NONE on success
 */
error_t bm8563_get_datetime(struct Device* device, struct RtcDateTime* dt);

/**
 * Write the date and time to the RTC.
 * @param[in] device bm8563 device
 * @param[in] dt     Pointer to RtcDateTime to write (year must be 2000–2199)
 * @return ERROR_NONE on success, ERROR_INVALID_ARGUMENT if any field is out of range
 */
error_t bm8563_set_datetime(struct Device* device, const struct RtcDateTime* dt);

#ifdef __cplusplus
}
#endif
