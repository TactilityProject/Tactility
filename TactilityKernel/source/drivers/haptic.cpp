// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/haptic.h>
#include <tactility/device.h>

#define HAPTIC_DRIVER_API(driver) ((HapticApi*)driver->api)

extern "C" {

error_t haptic_set_waveform(Device* device, uint8_t slot, uint8_t waveform) {
    const auto* driver = device_get_driver(device);
    return HAPTIC_DRIVER_API(driver)->set_waveform(device, slot, waveform);
}

error_t haptic_select_library(Device* device, uint8_t library) {
    const auto* driver = device_get_driver(device);
    return HAPTIC_DRIVER_API(driver)->select_library(device, library);
}

error_t haptic_start_playback(Device* device) {
    const auto* driver = device_get_driver(device);
    return HAPTIC_DRIVER_API(driver)->start_playback(device);
}

error_t haptic_stop_playback(Device* device) {
    const auto* driver = device_get_driver(device);
    return HAPTIC_DRIVER_API(driver)->stop_playback(device);
}

const DeviceType HAPTIC_TYPE {
    .name = "haptic"
};

}
