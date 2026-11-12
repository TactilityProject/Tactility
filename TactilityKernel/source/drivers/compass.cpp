// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/compass.h>
#include <tactility/device.h>

#include <cmath>

#define COMPASS_DRIVER_API(driver) ((struct CompassApi*)driver->api)

extern "C" {

error_t compass_read_field(Device* device, CompassData* data) {
    const auto* driver = device_get_driver(device);
    return COMPASS_DRIVER_API(driver)->read_field(device, data);
}

error_t compass_read_heading(Device* device, float* heading_deg) {
    const auto* driver = device_get_driver(device);
    auto* api = COMPASS_DRIVER_API(driver);
    if (api->read_heading) return api->read_heading(device, heading_deg);

    CompassData data;
    error_t error = api->read_field(device, &data);
    if (error != ERROR_NONE) return error;
    if (data.x == 0.0f && data.y == 0.0f) return ERROR_INVALID_STATE;

    float heading = std::atan2(data.y, data.x) * (180.0f / static_cast<float>(M_PI));
    if (heading < 0.0f) {
        heading += 360.0f;
        if (heading >= 360.0f) heading = 0.0f;
    }
    *heading_deg = heading;
    return ERROR_NONE;
}

const DeviceType COMPASS_TYPE {
    .name = "compass"
};

}
