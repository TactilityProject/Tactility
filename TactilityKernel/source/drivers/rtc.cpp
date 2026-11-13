// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/rtc.h>
#include <tactility/device.h>

#define RTC_DRIVER_API(driver) ((struct RtcApi*)driver->api)

extern "C" {

error_t rtc_get_time(Device* device, RtcDateTime* dt) {
    const auto* driver = device_get_driver(device);
    return RTC_DRIVER_API(driver)->get_time(device, dt);
}

error_t rtc_set_time(Device* device, const RtcDateTime* dt) {
    const auto* driver = device_get_driver(device);
    return RTC_DRIVER_API(driver)->set_time(device, dt);
}

bool rtc_datetime_is_valid(const RtcDateTime* dt, uint16_t min_year, uint16_t max_year) {
    if (dt->year < min_year || dt->year > max_year ||
        dt->month < 1 || dt->month > 12 ||
        dt->day < 1 ||
        dt->hour > 23 || dt->minute > 59 || dt->second > 59) {
        return false;
    }

    static constexpr uint8_t DAYS_IN_MONTH[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    const bool leap_year = (dt->year % 4 == 0 && dt->year % 100 != 0) || dt->year % 400 == 0;
    const uint8_t max_day = (dt->month == 2 && leap_year) ? 29 : DAYS_IN_MONTH[dt->month - 1];
    return dt->day <= max_day;
}

const DeviceType RTC_TYPE {
    .name = "rtc"
};

}
