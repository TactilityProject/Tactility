// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>
#include <tactility/error.h>

struct Device;

#ifdef __cplusplus
extern "C" {
#endif

struct Rx8130ceConfig {
    /** Address on bus */
    uint8_t address;
};

struct Rx8130ceDateTime {
    uint16_t year;   // 2000–2099
    uint8_t  month;  // 1–12
    uint8_t  day;    // 1–31
    uint8_t  hour;   // 0–23
    uint8_t  minute; // 0–59
    uint8_t  second; // 0–59
};

/**
 * Read the current date and time from the RTC.
 * @param[in]  device rx8130ce device
 * @param[out] dt     Pointer to Rx8130ceDateTime to populate
 * @return ERROR_NONE on success, ERROR_INVALID_STATE if VLF is set (clock data unreliable)
 */
error_t rx8130ce_get_datetime(struct Device* device, struct Rx8130ceDateTime* dt);

/**
 * Write the date and time to the RTC.
 * @param[in] device rx8130ce device
 * @param[in] dt     Pointer to Rx8130ceDateTime to write (year must be 2000–2099)
 * @return ERROR_NONE on success, ERROR_INVALID_ARGUMENT if any field is out of range
 */
error_t rx8130ce_set_datetime(struct Device* device, const struct Rx8130ceDateTime* dt);

#ifdef __cplusplus
}
#endif
