// SPDX-License-Identifier: Apache-2.0

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/root.h>
#include <cstring>

extern "C" {

bool root_is_model(const struct Device* device, const char* buffer) {
    auto* config = static_cast<const RootConfig*>(device->config);
    return strcmp(config->model, buffer) == 0;
}

Driver root_driver = {
    .name = "root",
    .compatible = (const char*[]) { "root", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .api = nullptr,
    .device_type = nullptr,
    .owner = nullptr,
    .internal = nullptr
};

}
