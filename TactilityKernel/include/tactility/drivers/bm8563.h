// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>
#include <tactility/error.h>

struct Device;

#ifdef __cplusplus
extern "C" {
#endif

struct Bm8563Config {
    /** Address on bus */
    uint8_t address;
};

struct Bm8563DateTime {
    uint16_t year;   // 2000–2199
    uint8_t  month;  // 1–12
    uint8_t  day;    // 1–31
    uint8_t  hour;   // 0–23
    uint8_t  minute; // 0–59
    uint8_t  second; // 0–59
};

/**
 * Read the current date and time from the RTC.
 * @param[in]  device bm8563 device
 * @param[out] dt     Pointer to Bm8563DateTime to populate
 * @return ERROR_NONE on success
 */
error_t bm8563_get_datetime(struct Device* device, struct Bm8563DateTime* dt);

/**
 * Write the date and time to the RTC.
 * @param[in] device bm8563 device
 * @param[in] dt     Pointer to Bm8563DateTime to write (year must be 2000–2199)
 * @return ERROR_NONE on success, ERROR_INVALID_ARGUMENT if any field is out of range
 */
error_t bm8563_set_datetime(struct Device* device, const struct Bm8563DateTime* dt);

#ifdef __cplusplus
}
#endif
