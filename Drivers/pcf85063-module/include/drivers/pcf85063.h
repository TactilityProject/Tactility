// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>
#include <tactility/drivers/rtc.h>
#include <tactility/error.h>

struct Device;

#ifdef __cplusplus
extern "C" {
#endif

struct Pcf85063Config {
    /** Address on bus */
    uint8_t address;
};

/**
 * Read the current date and time from the RTC.
 * @param[in]  device pcf85063 device
 * @param[out] dt     Pointer to RtcDateTime to populate
 * @return ERROR_NONE on success, ERROR_INVALID_STATE when the oscillator stopped since the time was set
 */
error_t pcf85063_get_datetime(struct Device* device, struct RtcDateTime* dt);

/**
 * Write the date and time to the RTC.
 * @param[in] device pcf85063 device
 * @param[in] dt     Pointer to RtcDateTime to write (year must be 2000 to 2099)
 * @return ERROR_NONE on success, ERROR_INVALID_ARGUMENT if any field is out of range
 */
error_t pcf85063_set_datetime(struct Device* device, const struct RtcDateTime* dt);

#ifdef __cplusplus
}
#endif
