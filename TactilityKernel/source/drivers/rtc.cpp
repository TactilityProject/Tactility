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

const DeviceType RTC_TYPE {
    .name = "rtc"
};

}
