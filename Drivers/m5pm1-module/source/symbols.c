// SPDX-License-Identifier: Apache-2.0
#include <drivers/m5pm1.h>
#include <tactility/module.h>

const struct ModuleSymbol m5pm1_module_symbols[] = {
    DEFINE_MODULE_SYMBOL(m5pm1_get_battery_voltage),
    DEFINE_MODULE_SYMBOL(m5pm1_get_vin_voltage),
    DEFINE_MODULE_SYMBOL(m5pm1_get_5vout_voltage),
    DEFINE_MODULE_SYMBOL(m5pm1_get_power_source),
    DEFINE_MODULE_SYMBOL(m5pm1_is_charging),
    DEFINE_MODULE_SYMBOL(m5pm1_set_charge_enable),
    DEFINE_MODULE_SYMBOL(m5pm1_set_boost_enable),
    DEFINE_MODULE_SYMBOL(m5pm1_set_ldo_enable),
    DEFINE_MODULE_SYMBOL(m5pm1_get_temperature),
    DEFINE_MODULE_SYMBOL(m5pm1_shutdown),
    DEFINE_MODULE_SYMBOL(m5pm1_reboot),
    DEFINE_MODULE_SYMBOL(m5pm1_btn_get_state),
    DEFINE_MODULE_SYMBOL(m5pm1_btn_get_flag),
    DEFINE_MODULE_SYMBOL(m5pm1_wdt_set),
    DEFINE_MODULE_SYMBOL(m5pm1_wdt_feed),
    DEFINE_MODULE_SYMBOL(m5pm1_read_rtc_ram),
    DEFINE_MODULE_SYMBOL(m5pm1_write_rtc_ram),
    DEFINE_MODULE_SYMBOL(m5pm1_set_led_count),
    DEFINE_MODULE_SYMBOL(m5pm1_set_led_color),
    DEFINE_MODULE_SYMBOL(m5pm1_refresh_leds),
    DEFINE_MODULE_SYMBOL(m5pm1_disable_leds),
    MODULE_SYMBOL_TERMINATOR
};
