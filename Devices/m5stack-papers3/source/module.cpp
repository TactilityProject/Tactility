#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

extern Driver papers3_power_driver;
extern Driver papers3_power_supply_driver;
extern Driver papers3_display_driver;

static error_t start() {
    check(driver_construct_add(&papers3_power_supply_driver) == ERROR_NONE);
    check(driver_construct_add(&papers3_power_driver) == ERROR_NONE);
    check(driver_construct_add(&papers3_display_driver) == ERROR_NONE);
    return ERROR_NONE;
}

static error_t stop() {
    check(driver_remove_destruct(&papers3_display_driver) == ERROR_NONE);
    check(driver_remove_destruct(&papers3_power_driver) == ERROR_NONE);
    check(driver_remove_destruct(&papers3_power_supply_driver) == ERROR_NONE);
    return ERROR_NONE;
}

Module m5stack_papers3_module = {
    .name = "m5stack-papers3",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
