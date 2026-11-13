// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <tactility/device.h>
#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

struct RtcDateTime {
    uint16_t year;   // 2000–2199
    uint8_t  month;  // 1–12
    uint8_t  day;    // 1–31
    uint8_t  hour;   // 0–23
    uint8_t  minute; // 0–59
    uint8_t  second; // 0–59
};

/**
 * @brief API for RTC (real-time clock) drivers.
 */
struct RtcApi {
    /**
     * @brief Reads the current date and time from the RTC.
     * @param[in] device the RTC device
     * @param[out] dt pointer to store the current date and time
     * @return ERROR_NONE on success
     */
    error_t (*get_time)(struct Device* device, struct RtcDateTime* dt);

    /**
     * @brief Writes the date and time to the RTC.
     * @param[in] device the RTC device
     * @param[in] dt the date and time to write
     * @return ERROR_NONE on success
     */
    error_t (*set_time)(struct Device* device, const struct RtcDateTime* dt);
};

/**
 * @brief Reads the current date and time using the specified RTC device.
 */
error_t rtc_get_time(struct Device* device, struct RtcDateTime* dt);

/**
 * @brief Writes the date and time using the specified RTC device.
 */
error_t rtc_set_time(struct Device* device, const struct RtcDateTime* dt);

/**
 * @brief Checks that all fields are in range, including the day against the month length and leap years.
 * @param[in] dt the date and time to check
 * @param[in] min_year lowest accepted year (inclusive)
 * @param[in] max_year highest accepted year (inclusive)
 * @return true when the date and time are valid
 */
bool rtc_datetime_is_valid(const struct RtcDateTime* dt, uint16_t min_year, uint16_t max_year);

extern const struct DeviceType RTC_TYPE;

#ifdef __cplusplus
}
#endif
