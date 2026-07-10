// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/pointer_placeholder.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/pointer.h>
#include <tactility/module.h>

extern "C" {

static error_t start(Device*) { return ERROR_NONE; }
static error_t stop(Device*) { return ERROR_NONE; }

extern Module root_module;

Driver pointer_placeholder_driver = {
    .name = "pointer_placeholder",
    .compatible = (const char*[]) { "pointer-placeholder", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = nullptr,
    .device_type = &POINTER_TYPE,
    .owner = &root_module,
    .internal = nullptr
};

}
