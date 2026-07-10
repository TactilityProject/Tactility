// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/power_supply.h>
#include <tactility/device.h>

#define POWER_SUPPLY_DRIVER_API(driver) ((PowerSupplyApi*)driver->api)

extern "C" {

bool power_supply_supports_property(Device* device, PowerSupplyProperty property) {
    const auto* driver = device_get_driver(device);
    return POWER_SUPPLY_DRIVER_API(driver)->supports_property(device, property);
}

error_t power_supply_get_property(Device* device, PowerSupplyProperty property, union PowerSupplyPropertyValue* out_value) {
    const auto* driver = device_get_driver(device);
    return POWER_SUPPLY_DRIVER_API(driver)->get_property(device, property, out_value);
}

bool power_supply_supports_charge_control(Device* device) {
    const auto* driver = device_get_driver(device);
    return POWER_SUPPLY_DRIVER_API(driver)->supports_charge_control(device);
}

bool power_supply_is_allowed_to_charge(Device* device) {
    const auto* driver = device_get_driver(device);
    return POWER_SUPPLY_DRIVER_API(driver)->is_allowed_to_charge(device);
}

error_t power_supply_set_allowed_to_charge(Device* device, bool allowed) {
    const auto* driver = device_get_driver(device);
    return POWER_SUPPLY_DRIVER_API(driver)->set_allowed_to_charge(device, allowed);
}

bool power_supply_supports_quick_charge(Device* device) {
    const auto* driver = device_get_driver(device);
    return POWER_SUPPLY_DRIVER_API(driver)->supports_quick_charge(device);
}

bool power_supply_is_quick_charge_enabled(Device* device) {
    const auto* driver = device_get_driver(device);
    return POWER_SUPPLY_DRIVER_API(driver)->is_quick_charge_enabled(device);
}

error_t power_supply_set_quick_charge_enabled(Device* device, bool enabled) {
    const auto* driver = device_get_driver(device);
    return POWER_SUPPLY_DRIVER_API(driver)->set_quick_charge_enabled(device, enabled);
}

bool power_supply_supports_power_off(Device* device) {
    const auto* driver = device_get_driver(device);
    return POWER_SUPPLY_DRIVER_API(driver)->supports_power_off(device);
}

error_t power_supply_power_off(Device* device) {
    const auto* driver = device_get_driver(device);
    return POWER_SUPPLY_DRIVER_API(driver)->power_off(device);
}

const DeviceType POWER_SUPPLY_TYPE {
    .name = "power_supply"
};

}
