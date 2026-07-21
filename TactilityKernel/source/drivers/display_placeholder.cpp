// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/display_placeholder.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/display.h>
#include <tactility/module.h>

extern "C" {

static error_t start(Device*) { return ERROR_NONE; }
static error_t stop(Device*) { return ERROR_NONE; }

extern Module root_module;

Driver display_placeholder_driver = {
    .name = "display_placeholder",
    .compatible = (const char*[]) { "display-placeholder", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = nullptr,
    .device_type = &DISPLAY_TYPE,
    .owner = &root_module,
    .internal = nullptr
};

}
