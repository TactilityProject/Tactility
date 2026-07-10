// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/rtc.h>
#include <tactility/device.h>

#define RTC_DRIVER_API(driver) ((struct RtcApi*)driver->api)

extern "C" {

error_t rtc_get_time(struct Device* device, struct RtcDateTime* dt) {
    const auto* driver = device_get_driver(device);
    return RTC_DRIVER_API(driver)->get_time(device, dt);
}

error_t rtc_set_time(struct Device* device, const struct RtcDateTime* dt) {
    const auto* driver = device_get_driver(device);
    return RTC_DRIVER_API(driver)->set_time(device, dt);
}

const struct DeviceType RTC_TYPE {
    .name = "rtc"
};

}
