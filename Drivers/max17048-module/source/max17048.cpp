// SPDX-License-Identifier: Apache-2.0
#include <drivers/max17048.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/log.h>
#include <tactility/Memory.hpp>


#define TAG "MAX17048"
#define GET_CONFIG(device) (static_cast<const Max17048Config*>((device)->config))
// The power-supply child's config pointer isn't set, it reads its settings from its parent (the max17048 device)
#define GET_PARENT_CONFIG(device) (static_cast<const Max17048Config*>(device_get_parent(device)->config))
#define GET_I2C_BUS(device) (device_get_parent(device_get_parent(device)))

static const TickType_t I2C_TIMEOUT = pdMS_TO_TICKS(50);

// The registers are 16 bits wide and big-endian on the wire
static constexpr uint8_t MAX17048_REG_VCELL = 0x02;
static constexpr uint8_t MAX17048_REG_SOC = 0x04;
static constexpr uint8_t MAX17048_REG_CRATE = 0x16;

extern "C" {

// region PowerSupplyApi (power-supply child device)

static bool supports_property(Device*, PowerSupplyProperty property) {
    switch (property) {
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
            uint16_t raw = 0;
            if (i2c_controller_register16be_get(i2c, config->address, MAX17048_REG_VCELL, &raw, I2C_TIMEOUT) != ERROR_NONE) {
                return ERROR_NOT_FOUND;
            }
            // 78.125 microvolt per step, which is 5/64 millivolt
            out_value->int_value = static_cast<int32_t>((static_cast<uint32_t>(raw) * 5) / 64);
            return ERROR_NONE;
        }
        case POWER_SUPPLY_PROP_CAPACITY: {
            uint16_t raw = 0;
            if (i2c_controller_register16be_get(i2c, config->address, MAX17048_REG_SOC, &raw, I2C_TIMEOUT) != ERROR_NONE) {
                return ERROR_NOT_FOUND;
            }
            // The high byte is the percentage and the low byte is 1/256 percent
            const int32_t percentage = raw >> 8;
            out_value->int_value = percentage > 100 ? 100 : percentage;
            return ERROR_NONE;
        }
        case POWER_SUPPLY_PROP_IS_CHARGING: {
            uint16_t raw = 0;
            if (i2c_controller_register16be_get(i2c, config->address, MAX17048_REG_CRATE, &raw, I2C_TIMEOUT) != ERROR_NONE) {
                return ERROR_NOT_FOUND;
            }
            // The charge/discharge rate is signed, and positive while the battery is charging
            out_value->int_value = static_cast<int16_t>(raw) > 0 ? 1 : 0;
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

static constexpr PowerSupplyApi MAX17048_POWER_SUPPLY_API = {
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

// Registered so that driver_bind() has a valid internal, but never matched against a devicetree node:
// max17048_driver wires it up directly by pointer.
Driver max17048_power_supply_driver = {
    .name = "max17048-power-supply",
    .compatible = (const char*[]) { "max17048-power-supply", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .probe = nullptr,
    .api = &MAX17048_POWER_SUPPLY_API,
    .device_type = &POWER_SUPPLY_TYPE,
    .internal = nullptr
};

// endregion

// region Driver lifecycle (max17048 device itself)

struct Max17048Internal {
    Device* power_supply_device = nullptr;
};

static error_t create_power_supply_child(Device* parent, Device*& out_child) {
    auto* child = tt::newObject<Device>();
    if (child == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    *child = Device { .address = 0, .name = "max17048-power-supply", .config = nullptr, .parent = nullptr, .flags = 0, .internal = nullptr };

    error_t error = device_construct(child);
    if (error != ERROR_NONE) {
        tt::deleteObject(child);
        return error;
    }

    device_set_parent(child, parent);
    device_set_driver(child, &max17048_power_supply_driver);

    error = device_add(child);
    if (error != ERROR_NONE) {
        device_destruct(child);
        tt::deleteObject(child);
        return error;
    }

    error = device_start(child);
    if (error != ERROR_NONE) {
        device_remove(child);
        device_destruct(child);
        tt::deleteObject(child);
        return error;
    }

    out_child = child;
    return ERROR_NONE;
}

static void destroy_power_supply_child(Device* child) {
    if (device_stop(child) != ERROR_NONE || device_remove(child) != ERROR_NONE) {
        LOG_E(TAG, "Failed to stop the power supply device");
        return;
    }
    // A client can still hold a reference, in which case the device can't be destructed and must stay allocated
    if (device_destruct(child) != ERROR_NONE) {
        LOG_E(TAG, "The power supply device is still in use, so it stays allocated");
        return;
    }
    tt::deleteObject(child);
}

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    check(device_get_type(parent) == &I2C_CONTROLLER_TYPE);

    auto* internal = tt::newObject<Max17048Internal>();
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    error_t error = create_power_supply_child(device, internal->power_supply_device);
    if (error != ERROR_NONE) {
        tt::deleteObject(internal);
        return error;
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Max17048Internal*>(device_get_driver_data(device));
    destroy_power_supply_child(internal->power_supply_device);
    device_set_driver_data(device, nullptr);
    tt::deleteObject(internal);
    return ERROR_NONE;
}

Driver max17048_driver = {
    .name = "max17048",
    .compatible = (const char*[]) { "maxim,max17048", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = nullptr,
    .device_type = nullptr,
    .internal = nullptr
};

// endregion

}
