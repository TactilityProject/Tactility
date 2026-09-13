// SPDX-License-Identifier: Apache-2.0
#include "cl32_v4_power.h"
#include "cl32_v4.h"

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/log.h>
#include <tactility/module.h>

#include <new>

extern Module cl32_module;

constexpr auto* TAG = "cl32-v4-power";

constexpr uint8_t REG_VOLTAGE = 0x14;
constexpr int MV_PER_LSB = 25;
constexpr int MIN_MV = 3200;
constexpr int MAX_MV = 4200;

// ---------------------------------------------------------------------------
// Power supply
// ---------------------------------------------------------------------------

static bool ps_supports_property(Device*, PowerSupplyProperty property) {
    return property == POWER_SUPPLY_PROP_VOLTAGE || property == POWER_SUPPLY_PROP_CAPACITY;
}

static error_t read_battery_mv(Device* chip_device, int* out_mv) {
    auto* i2c0 = device_get_parent(chip_device);
    uint8_t raw = 0;
    error_t error = i2c_controller_register8_get(i2c0, CL32_V4_CORE_I2C_ADDRESS, REG_VOLTAGE, &raw, CL32_V4_CORE_TIMEOUT);
    if (error != ERROR_NONE) {
        return error;
    }
    *out_mv = raw * MV_PER_LSB;
    return ERROR_NONE;
}

static int estimate_capacity_from_mv(int battery_mv) {
    if (battery_mv <= MIN_MV) return 0;
    if (battery_mv >= MAX_MV) return 100;
    return (battery_mv - MIN_MV) * 100 / (MAX_MV - MIN_MV);
}

static error_t ps_get_property(Device* device, PowerSupplyProperty property, PowerSupplyPropertyValue* out_value) {
    if (property != POWER_SUPPLY_PROP_VOLTAGE && property != POWER_SUPPLY_PROP_CAPACITY) {
        return ERROR_NOT_SUPPORTED;
    }

    int battery_mv;
    error_t error = read_battery_mv(device_get_parent(device), &battery_mv);
    if (error != ERROR_NONE) {
        return error;
    }

    out_value->int_value = (property == POWER_SUPPLY_PROP_VOLTAGE) ? battery_mv : estimate_capacity_from_mv(battery_mv);
    return ERROR_NONE;
}

static bool ps_supports_charge_control(Device*) { return false; }
static bool ps_is_allowed_to_charge(Device*) { return false; }
static error_t ps_set_allowed_to_charge(Device*, bool) { return ERROR_NOT_SUPPORTED; }
static bool ps_supports_quick_charge(Device*) { return false; }
static bool ps_is_quick_charge_enabled(Device*) { return false; }
static error_t ps_set_quick_charge_enabled(Device*, bool) { return ERROR_NOT_SUPPORTED; }
static bool ps_supports_power_off(Device*) { return false; }
static error_t ps_power_off(Device*) { return ERROR_NOT_SUPPORTED; }

static constexpr PowerSupplyApi CL32_V4_POWER_SUPPLY_API = {
    .supports_property = ps_supports_property,
    .get_property = ps_get_property,
    .supports_charge_control = ps_supports_charge_control,
    .is_allowed_to_charge = ps_is_allowed_to_charge,
    .set_allowed_to_charge = ps_set_allowed_to_charge,
    .supports_quick_charge = ps_supports_quick_charge,
    .is_quick_charge_enabled = ps_is_quick_charge_enabled,
    .set_quick_charge_enabled = ps_set_quick_charge_enabled,
    .supports_power_off = ps_supports_power_off,
    .power_off = ps_power_off,
};

// Registered in cl32_module's driver list so driver_bind() has a valid ->internal, but never
// matched against a devicetree node: wired up directly by pointer from cl32_v4_power_driver's start().
Driver cl32_v4_power_supply_driver = {
    .name = "cl32-v4-power-supply",
    .compatible = (const char*[]) { "cl32-v4-power-supply", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .api = &CL32_V4_POWER_SUPPLY_API,
    .device_type = &POWER_SUPPLY_TYPE,
    .owner = &cl32_module,
    .internal = nullptr
};

static error_t create_power_supply_child(Device* parent, Device*& out_child) {
    auto* child = new(std::nothrow) Device { .address = 0, .name = "cl32-v4-power-supply", .config = nullptr, .parent = nullptr, .flags = 0, .internal = nullptr };
    if (child == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    error_t error = device_construct(child);
    if (error != ERROR_NONE) {
        delete child;
        return error;
    }

    device_set_parent(child, parent);
    device_set_driver(child, &cl32_v4_power_supply_driver);

    error = device_add(child);
    if (error != ERROR_NONE) {
        device_destruct(child);
        delete child;
        return error;
    }

    error = device_start(child);
    if (error != ERROR_NONE) {
        device_remove(child);
        device_destruct(child);
        delete child;
        return error;
    }

    out_child = child;
    return ERROR_NONE;
}

static void destroy_power_supply_child(Device* child) {
    check(device_stop(child) == ERROR_NONE);
    check(device_remove(child) == ERROR_NONE);
    check(device_destruct(child) == ERROR_NONE);
    delete child;
}

// ---------------------------------------------------------------------------
// Chip driver
// ---------------------------------------------------------------------------

struct Cl32V4PowerInternal {
    Device* power_supply_device = nullptr;
};

static error_t start(Device* device) {
    if (device_get_type(device_get_parent(device)) != &I2C_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent is not an I2C controller");
        return ERROR_RESOURCE;
    }

    auto* internal = new(std::nothrow) Cl32V4PowerInternal();
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    error_t error = create_power_supply_child(device, internal->power_supply_device);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to create power-supply device");
        delete internal;
        return error;
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Cl32V4PowerInternal*>(device_get_driver_data(device));
    destroy_power_supply_child(internal->power_supply_device);
    device_set_driver_data(device, nullptr);
    delete internal;
    return ERROR_NONE;
}

Driver cl32_v4_power_driver = {
    .name = "cl32-v4-power",
    .compatible = (const char*[]) { "cl32-v4-power", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = nullptr,
    .device_type = nullptr,
    .owner = &cl32_module,
    .internal = nullptr
};
