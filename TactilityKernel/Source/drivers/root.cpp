// SPDX-License-Identifier: Apache-2.0

#include <tactility/driver.h>
#include <tactility/drivers/root.h>

extern "C" {

Driver root_driver = {
    .name = "root",
    .compatible = (const char*[]) { "root", nullptr },
    .startDevice = nullptr,
    .stopDevice = nullptr,
    .api = nullptr,
    .deviceType = nullptr,
    .owner = nullptr,
    .internal = nullptr
};

}
