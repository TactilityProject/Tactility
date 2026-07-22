// SPDX-License-Identifier: Apache-2.0
#include <drivers/axp2101.h>
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver axp2101_driver;
extern Driver axp2101_power_supply_driver;
extern Driver axp2101_backlight_driver;

static const Driver* axp2101_drivers[] = {
    &axp2101_driver,
    &axp2101_power_supply_driver,
    &axp2101_backlight_driver,
    nullptr
};

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

Module axp2101_module = {
    .name = "axp2101",
    .drivers = axp2101_drivers,
    .symbols = axp2101_module_symbols
};

}
