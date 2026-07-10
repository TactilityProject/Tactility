// SPDX-License-Identifier: Apache-2.0
#pragma once

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

extern const struct DeviceType RTC_TYPE;

#ifdef __cplusplus
}
#endif
