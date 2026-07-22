// SPDX-License-Identifier: Apache-2.0
#include <drivers/axp192.h>
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver axp192_driver;
extern Driver axp192_power_supply_driver;
extern Driver axp192_backlight_driver;

static const Driver* axp192_drivers[] = {
    &axp192_driver,
    &axp192_power_supply_driver,
    &axp192_backlight_driver,
    nullptr
};

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

Module axp192_module = {
    .name = "axp192",
    .drivers = axp192_drivers,
    .symbols = axp192_module_symbols,
    .internal = nullptr
};

}
