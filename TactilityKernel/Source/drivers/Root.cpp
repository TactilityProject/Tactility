// SPDX-License-Identifier: Apache-2.0

#include <Tactility/drivers/Root.h>
#include <Tactility/Driver.h>

extern "C" {

Driver root_driver = {
    .name = "root",
    .compatible = (const char*[]) { "root", nullptr },
    .startDevice = nullptr,
    .stopDevice = nullptr,
    .api = nullptr,
    .deviceType = nullptr,
    .internal = { 0 }
};

}
