// SPDX-License-Identifier: Apache-2.0
#include <drivers/axp192.h>
#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver axp192_driver;
extern Driver axp192_power_supply_driver;
extern Driver axp192_backlight_driver;

const struct ModuleSymbol axp192_module_symbols[] = {
    DEFINE_MODULE_SYMBOL(axp192_is_rail_enabled),
    DEFINE_MODULE_SYMBOL(axp192_set_rail_enabled),
    DEFINE_MODULE_SYMBOL(axp192_set_rail_voltage),
    DEFINE_MODULE_SYMBOL(axp192_get_battery_voltage),
    DEFINE_MODULE_SYMBOL(axp192_get_battery_charge_current),
    DEFINE_MODULE_SYMBOL(axp192_get_battery_discharge_current),
    DEFINE_MODULE_SYMBOL(axp192_get_battery_power),
    DEFINE_MODULE_SYMBOL(axp192_is_charging),
    DEFINE_MODULE_SYMBOL(axp192_is_charge_enabled),
    DEFINE_MODULE_SYMBOL(axp192_set_charge_enabled),
    DEFINE_MODULE_SYMBOL(axp192_power_off),
    DEFINE_MODULE_SYMBOL(axp192_set_gpio1_pwm1_output),
    DEFINE_MODULE_SYMBOL(axp192_set_pwm1_duty_cycle),
    MODULE_SYMBOL_TERMINATOR
};

static error_t start() {
    /* We crash when construct fails, because if a single driver fails to construct,
     * there is no guarantee that the previously constructed drivers can be destroyed */
    check(driver_construct_add(&axp192_driver) == ERROR_NONE);
    check(driver_construct_add(&axp192_power_supply_driver) == ERROR_NONE);
    check(driver_construct_add(&axp192_backlight_driver) == ERROR_NONE);
    return ERROR_NONE;
}

static error_t stop() {
    /* We crash when destruct fails, because if a single driver fails to destruct,
     * there is no guarantee that the previously destroyed drivers can be recovered */
    check(driver_remove_destruct(&axp192_backlight_driver) == ERROR_NONE);
    check(driver_remove_destruct(&axp192_power_supply_driver) == ERROR_NONE);
    check(driver_remove_destruct(&axp192_driver) == ERROR_NONE);
    return ERROR_NONE;
}

Module axp192_module = {
    .name = "axp192",
    .start = start,
    .stop = stop,
    .symbols = axp192_module_symbols,
    .internal = nullptr
};

}
