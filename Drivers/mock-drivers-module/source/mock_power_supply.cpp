// SPDX-License-Identifier: Apache-2.0
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/power_supply.h>

#include <atomic>

// region PowerSupplyApi

static std::atomic<bool> allowed_to_charge { true };

static bool supports_property(Device*, PowerSupplyProperty property) {
    switch (property) {
        case POWER_SUPPLY_PROP_IS_CHARGING:
        case POWER_SUPPLY_PROP_CURRENT:
        case POWER_SUPPLY_PROP_VOLTAGE:
        case POWER_SUPPLY_PROP_CAPACITY:
            return true;
        default:
            return false;
    }
}

static error_t get_property(Device*, PowerSupplyProperty property, PowerSupplyPropertyValue* out_value) {
    const bool charging = allowed_to_charge.load();
    switch (property) {
        case POWER_SUPPLY_PROP_IS_CHARGING:
            out_value->int_value = charging ? 1 : 0;
            return ERROR_NONE;
        case POWER_SUPPLY_PROP_CURRENT:
            out_value->int_value = charging ? 500 : -150;
            return ERROR_NONE;
        case POWER_SUPPLY_PROP_VOLTAGE:
            out_value->int_value = 3950;
            return ERROR_NONE;
        case POWER_SUPPLY_PROP_CAPACITY:
            out_value->int_value = 80;
            return ERROR_NONE;
        default:
            return ERROR_NOT_SUPPORTED;
    }
}

static bool supports_charge_control(Device*) { return true; }
static bool is_allowed_to_charge(Device*) { return allowed_to_charge.load(); }

static error_t set_allowed_to_charge(Device*, bool allowed) {
    allowed_to_charge.store(allowed);
    return ERROR_NONE;
}

static bool supports_quick_charge(Device*) { return false; }
static bool is_quick_charge_enabled(Device*) { return false; }
static error_t set_quick_charge_enabled(Device*, bool) { return ERROR_NOT_SUPPORTED; }
static bool supports_power_off(Device*) { return false; }
static error_t power_off(Device*) { return ERROR_NOT_SUPPORTED; }

// endregion

static const PowerSupplyApi mock_power_supply_api = {
    .supports_property = supports_property,
    .get_property = get_property,
    .supports_charge_control = supports_charge_control,
    .is_allowed_to_charge = is_allowed_to_charge,
    .set_allowed_to_charge = set_allowed_to_charge,
    .supports_quick_charge = supports_quick_charge,
    .is_quick_charge_enabled = is_quick_charge_enabled,
    .set_quick_charge_enabled = set_quick_charge_enabled,
    .supports_power_off = supports_power_off,
    .power_off = power_off,
};

extern "C" {

Driver mock_power_supply_driver = {
    .name = "mock_power_supply",
    .compatible = (const char*[]) { "mock,power-supply", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .api = (const void*)&mock_power_supply_api,
    .device_type = &POWER_SUPPLY_TYPE,
    .internal = nullptr
};

} // extern "C"
