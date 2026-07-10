// SPDX-License-Identifier: Apache-2.0
#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver posix_wifi_driver;

static error_t start() {
    check(driver_construct_add(&posix_wifi_driver) == ERROR_NONE);
    return ERROR_NONE;
}

static error_t stop() {
    check(driver_remove_destruct(&posix_wifi_driver) == ERROR_NONE);
    return ERROR_NONE;
}

struct Module platform_posix_module = {
    .name = "platform-posix",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
