// SPDX-License-Identifier: Apache-2.0

#include <tactility/driver.h>
#include <tactility/drivers/root.h>

extern "C" {

Driver root_driver = {
    .name = "root",
    .compatible = (const char*[]) { "root", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .api = nullptr,
    .device_type = nullptr,
    .owner = nullptr
};

}
