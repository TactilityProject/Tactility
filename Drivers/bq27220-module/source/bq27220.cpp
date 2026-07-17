// SPDX-License-Identifier: Apache-2.0
#include <drivers/bq27220.h>
#include <bq27220_module.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/log.h>

#include <new>

#define TAG "BQ27220"
#define GET_CONFIG(device) (static_cast<const Bq27220Config*>((device)->config))
// The power-supply child's config pointer isn't set; it reads its settings from its parent
// (the bq27220 device) instead - same convention as the sy6970 driver.
#define GET_PARENT_CONFIG(device) (static_cast<const Bq27220Config*>(device_get_parent(device)->config))
#define GET_I2C_BUS(device) (device_get_parent(device_get_parent(device)))

static const TickType_t I2C_TIMEOUT = pdMS_TO_TICKS(50);

// BQ27220 standard command registers (16-bit, little-endian on the wire).
static constexpr uint8_t BQ27220_REG_VOLTAGE = 0x08;
static constexpr uint8_t BQ27220_REG_BATTERY_STATUS = 0x0A;
static constexpr uint8_t BQ27220_REG_CURRENT = 0x0C;
static constexpr uint8_t BQ27220_REG_STATE_OF_CHARGE = 0x2C;
// BatteryStatus bit 0: DSG (device is in DISCHARGE). Mirrors the deprecated HAL's
// TpagerPower/TdeckmaxPower fallback: "is charging" = not discharging.
static constexpr uint16_t BQ27220_BATTERY_STATUS_DSG = 1u << 0;

extern "C" {

// region PowerSupplyApi (power-supply child device)

static bool supports_property(Device*, PowerSupplyProperty property) {
    switch (property) {
        case POWER_SUPPLY_PROP_CURRENT:
        case POWER_SUPPLY_PROP_VOLTAGE:
        case POWER_SUPPLY_PROP_CAPACITY:
        case POWER_SUPPLY_PROP_IS_CHARGING:
            return true;
        default:
            return false;
    }
}

static error_t get_property(Device* device, PowerSupplyProperty property, PowerSupplyPropertyValue* out_value) {
    auto* i2c = GET_I2C_BUS(device);
    const auto* config = GET_PARENT_CONFIG(device);

    switch (property) {
        case POWER_SUPPLY_PROP_VOLTAGE: {
            uint16_t voltage_mv = 0;
            if (i2c_controller_register16le_get(i2c, config->address, BQ27220_REG_VOLTAGE, &voltage_mv, I2C_TIMEOUT) != ERROR_NONE) {
                return ERROR_NOT_FOUND;
            }
            out_value->int_value = voltage_mv;
            return ERROR_NONE;
        }
        case POWER_SUPPLY_PROP_CURRENT: {
            uint16_t raw = 0;
            if (i2c_controller_register16le_get(i2c, config->address, BQ27220_REG_CURRENT, &raw, I2C_TIMEOUT) != ERROR_NONE) {
                return ERROR_NOT_FOUND;
            }
            out_value->int_value = static_cast<int16_t>(raw);
            return ERROR_NONE;
        }
        case POWER_SUPPLY_PROP_CAPACITY: {
            uint16_t soc = 0;
            if (i2c_controller_register16le_get(i2c, config->address, BQ27220_REG_STATE_OF_CHARGE, &soc, I2C_TIMEOUT) != ERROR_NONE) {
                return ERROR_NOT_FOUND;
            }
            out_value->int_value = soc;
            return ERROR_NONE;
        }
        case POWER_SUPPLY_PROP_IS_CHARGING: {
            uint16_t status = 0;
            if (i2c_controller_register16le_get(i2c, config->address, BQ27220_REG_BATTERY_STATUS, &status, I2C_TIMEOUT) != ERROR_NONE) {
                return ERROR_NOT_FOUND;
            }
            out_value->int_value = (status & BQ27220_BATTERY_STATUS_DSG) == 0 ? 1 : 0;
            return ERROR_NONE;
        }
        default:
            return ERROR_NOT_SUPPORTED;
    }
}

static bool supports_charge_control(Device*) { return false; }
static bool is_allowed_to_charge(Device*) { return false; }
static error_t set_allowed_to_charge(Device*, bool) { return ERROR_NOT_SUPPORTED; }
static bool supports_quick_charge(Device*) { return false; }
static bool is_quick_charge_enabled(Device*) { return false; }
static error_t set_quick_charge_enabled(Device*, bool) { return ERROR_NOT_SUPPORTED; }
static bool supports_power_off(Device*) { return false; }
static error_t power_off(Device*) { return ERROR_NOT_SUPPORTED; }

static constexpr PowerSupplyApi BQ27220_POWER_SUPPLY_API = {
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

// Registered (driver_construct_add() in module.cpp) so driver_bind() has a valid ->internal, but
// never matched against a devicetree node: bq27220_driver wires it up directly by pointer.
Driver bq27220_power_supply_driver = {
    .name = "bq27220-power-supply",
    .compatible = (const char*[]) { "bq27220-power-supply", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .api = &BQ27220_POWER_SUPPLY_API,
    .device_type = &POWER_SUPPLY_TYPE,
    .owner = &bq27220_module,
    .internal = nullptr
};

// endregion

// region Driver lifecycle (bq27220 device itself)

struct Bq27220Internal {
    Device* power_supply_device = nullptr;
};

static error_t create_power_supply_child(Device* parent, Device*& out_child) {
    auto* child = new (std::nothrow) Device { .address = 0, .name = "bq27220-power-supply", .config = nullptr, .parent = nullptr, .internal = nullptr };
    if (child == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    error_t error = device_construct(child);
    if (error != ERROR_NONE) {
        delete child;
        return error;
    }

    device_set_parent(child, parent);
    device_set_driver(child, &bq27220_power_supply_driver);

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

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    check(device_get_type(parent) == &I2C_CONTROLLER_TYPE);

    auto* internal = new (std::nothrow) Bq27220Internal();
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    error_t error = create_power_supply_child(device, internal->power_supply_device);
    if (error != ERROR_NONE) {
        delete internal;
        return error;
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Bq27220Internal*>(device_get_driver_data(device));
    destroy_power_supply_child(internal->power_supply_device);
    device_set_driver_data(device, nullptr);
    delete internal;
    return ERROR_NONE;
}

Driver bq27220_driver = {
    .name = "bq27220",
    .compatible = (const char*[]) { "ti,bq27220", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = nullptr,
    .device_type = nullptr,
    .owner = &bq27220_module,
    .internal = nullptr
};

// endregion

}
