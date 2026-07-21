// SPDX-License-Identifier: Apache-2.0
#include <drivers/axp2101.h>
#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver axp2101_driver;
extern Driver axp2101_power_supply_driver;
extern Driver axp2101_backlight_driver;

const struct ModuleSymbol axp2101_module_symbols[] = {
    DEFINE_MODULE_SYMBOL(axp2101_is_dcdc_enabled),
    DEFINE_MODULE_SYMBOL(axp2101_set_dcdc_enabled),
    DEFINE_MODULE_SYMBOL(axp2101_set_dcdc_voltage),
    DEFINE_MODULE_SYMBOL(axp2101_is_ldo_enabled),
    DEFINE_MODULE_SYMBOL(axp2101_set_ldo_enabled),
    DEFINE_MODULE_SYMBOL(axp2101_set_ldo_voltage),
    DEFINE_MODULE_SYMBOL(axp2101_get_battery_voltage),
    DEFINE_MODULE_SYMBOL(axp2101_is_battery_connected),
    DEFINE_MODULE_SYMBOL(axp2101_is_vbus_present),
    DEFINE_MODULE_SYMBOL(axp2101_get_vbus_voltage),
    DEFINE_MODULE_SYMBOL(axp2101_is_charging),
    DEFINE_MODULE_SYMBOL(axp2101_is_charge_enabled),
    DEFINE_MODULE_SYMBOL(axp2101_set_charge_enabled),
    DEFINE_MODULE_SYMBOL(axp2101_power_off),
    MODULE_SYMBOL_TERMINATOR
};

static error_t start() {
    /* We crash when construct fails, because if a single driver fails to construct,
     * there is no guarantee that the previously constructed drivers can be destroyed */
    check(driver_construct_add(&axp2101_driver) == ERROR_NONE);
    check(driver_construct_add(&axp2101_power_supply_driver) == ERROR_NONE);
    check(driver_construct_add(&axp2101_backlight_driver) == ERROR_NONE);
    return ERROR_NONE;
}

static error_t stop() {
    /* We crash when destruct fails, because if a single driver fails to destruct,
     * there is no guarantee that the previously destroyed drivers can be recovered */
    check(driver_remove_destruct(&axp2101_backlight_driver) == ERROR_NONE);
    check(driver_remove_destruct(&axp2101_power_supply_driver) == ERROR_NONE);
    check(driver_remove_destruct(&axp2101_driver) == ERROR_NONE);
    return ERROR_NONE;
}

Module axp2101_module = {
    .name = "axp2101",
    .start = start,
    .stop = stop,
    .symbols = axp2101_module_symbols,
    .internal = nullptr
};

}
