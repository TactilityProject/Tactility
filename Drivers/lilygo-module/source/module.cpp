// SPDX-License-Identifier: Apache-2.0
#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver tdeck_keyboard_driver;
extern Driver tdeck_keyboard_backlight_driver;
extern Driver tdeck_trackball_driver;
extern Driver tpager_encoder_driver;

static error_t start() {
    /* We crash when construct fails, because if a single driver fails to construct,
     * there is no guarantee that the previously constructed drivers can be destroyed */
    check(driver_construct_add(&tdeck_keyboard_driver) == ERROR_NONE);
    check(driver_construct_add(&tdeck_keyboard_backlight_driver) == ERROR_NONE);
    check(driver_construct_add(&tdeck_trackball_driver) == ERROR_NONE);
    check(driver_construct_add(&tpager_encoder_driver) == ERROR_NONE);
    return ERROR_NONE;
}

static error_t stop() {
    /* We crash when destruct fails, because if a single driver fails to destruct,
     * there is no guarantee that the previously destroyed drivers can be recovered */
    check(driver_remove_destruct(&tdeck_keyboard_driver) == ERROR_NONE);
    check(driver_remove_destruct(&tdeck_keyboard_backlight_driver) == ERROR_NONE);
    check(driver_remove_destruct(&tdeck_trackball_driver) == ERROR_NONE);
    check(driver_remove_destruct(&tpager_encoder_driver) == ERROR_NONE);
    return ERROR_NONE;
}

Module lilygo_module = {
    .name = "lilygo",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
