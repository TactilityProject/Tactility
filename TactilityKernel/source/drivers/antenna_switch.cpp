// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/antenna_switch.h>

#include <tactility/device.h>
#include <tactility/driver.h>

error_t antenna_switch_set_antenna(Device* device, uint32_t antenna) {
    auto* driver = device_get_driver(device);
    if (driver == nullptr || driver->api == nullptr) return ERROR_NOT_SUPPORTED;

    auto* api = static_cast<const AntennaSwitchApi*>(driver->api);
    if (api->set_antenna == nullptr) return ERROR_NOT_SUPPORTED;
    return api->set_antenna(device, antenna);
}

error_t antenna_switch_get_antenna(Device* device, uint32_t* antenna) {
    auto* driver = device_get_driver(device);
    if (driver == nullptr || driver->api == nullptr) return ERROR_NOT_SUPPORTED;

    auto* api = static_cast<const AntennaSwitchApi*>(driver->api);
    if (api->get_antenna == nullptr) return ERROR_NOT_SUPPORTED;
    return api->get_antenna(device, antenna);
}

const DeviceType ANTENNA_SWITCH_TYPE = {
    .name = "antenna_switch"
};
