// SPDX-License-Identifier: Apache-2.0
#include <drivers/bq25896.h>
#include <bq25896_module.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/log.h>

#include <new>

#define TAG "BQ25896"
#define GET_CONFIG(device) (static_cast<const Bq25896Config*>((device)->config))
// The power-supply child's config pointer isn't set; it reads its settings from its parent
// (the bq25896 device) instead - same convention as the sy6970/bq27220 drivers.
#define GET_PARENT_CONFIG(device) (static_cast<const Bq25896Config*>(device_get_parent(device)->config))
#define GET_I2C_BUS(device) (device_get_parent(device_get_parent(device)))

static const TickType_t I2C_TIMEOUT = pdMS_TO_TICKS(50);

// REG09 bit5: BATFET_DIS. 0 = BATFET on (normal operation), 1 = BATFET off (ship mode).
// Register offset is shared with the SY6970 (see that driver's own comment).
static constexpr uint8_t REG_09 = 0x09;
static constexpr uint8_t REG09_BATFET_DIS = 1u << 5;

extern "C" {

// region PowerSupplyApi (power-supply child device)

// Matches the deprecated HAL's Bq25896: no charging status/metrics were ever read from this
// chip (those came from a separate fuel gauge, if present - see the bq27220 driver), so the
// only capability exposed here is ship-mode power-off.
static bool supports_property(Device*, PowerSupplyProperty) { return false; }
static error_t get_property(Device*, PowerSupplyProperty, PowerSupplyPropertyValue*) { return ERROR_NOT_SUPPORTED; }
static bool supports_charge_control(Device*) { return false; }
static bool is_allowed_to_charge(Device*) { return false; }
static error_t set_allowed_to_charge(Device*, bool) { return ERROR_NOT_SUPPORTED; }
static bool supports_quick_charge(Device*) { return false; }
static bool is_quick_charge_enabled(Device*) { return false; }
static error_t set_quick_charge_enabled(Device*, bool) { return ERROR_NOT_SUPPORTED; }

static bool supports_power_off(Device*) { return true; }

static error_t power_off(Device* device) {
    auto* i2c = GET_I2C_BUS(device);
    const auto* config = GET_PARENT_CONFIG(device);
    LOG_I(TAG, "Power off (BATFET_DIS)");
    return i2c_controller_register8_set_bits(i2c, config->address, REG_09, REG09_BATFET_DIS, I2C_TIMEOUT);
}

static constexpr PowerSupplyApi BQ25896_POWER_SUPPLY_API = {
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
// never matched against a devicetree node: bq25896_driver wires it up directly by pointer.
Driver bq25896_power_supply_driver = {
    .name = "bq25896-power-supply",
    .compatible = (const char*[]) { "bq25896-power-supply", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .api = &BQ25896_POWER_SUPPLY_API,
    .device_type = &POWER_SUPPLY_TYPE,
    .owner = &bq25896_module,
    .internal = nullptr
};

// endregion

// region Driver lifecycle (bq25896 device itself)

struct Bq25896Internal {
    Device* power_supply_device = nullptr;
};

static error_t create_power_supply_child(Device* parent, Device*& out_child) {
    auto* child = new (std::nothrow) Device { .address = 0, .name = "bq25896-power-supply", .config = nullptr, .parent = nullptr, .internal = nullptr };
    if (child == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    error_t error = device_construct(child);
    if (error != ERROR_NONE) {
        delete child;
        return error;
    }

    device_set_parent(child, parent);
    device_set_driver(child, &bq25896_power_supply_driver);

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

    const auto* config = GET_CONFIG(device);

    // Matches the deprecated HAL's Bq25896::powerOn(): ensure the BATFET is on (normal
    // operation), in case a previous run left it in ship mode.
    LOG_I(TAG, "Power on");
    i2c_controller_register8_reset_bits(parent, config->address, REG_09, REG09_BATFET_DIS, I2C_TIMEOUT);

    auto* internal = new (std::nothrow) Bq25896Internal();
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
    auto* internal = static_cast<Bq25896Internal*>(device_get_driver_data(device));
    destroy_power_supply_child(internal->power_supply_device);
    device_set_driver_data(device, nullptr);
    delete internal;
    return ERROR_NONE;
}

Driver bq25896_driver = {
    .name = "bq25896",
    .compatible = (const char*[]) { "ti,bq25896", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = nullptr,
    .device_type = nullptr,
    .owner = &bq25896_module,
    .internal = nullptr
};

// endregion

}
