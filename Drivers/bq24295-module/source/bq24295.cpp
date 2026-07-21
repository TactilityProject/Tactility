// SPDX-License-Identifier: Apache-2.0
#include <drivers/bq24295.h>
#include <bq24295_module.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/log.h>

#include <new>

#define TAG "BQ24295"
#define GET_CONFIG(device) (static_cast<const Bq24295Config*>((device)->config))

/** Reference:
 * https://www.ti.com/lit/ds/symlink/bq24295.pdf
 * https://gitlab.com/hamishcunningham/unphonelibrary/-/blob/main/unPhone.h?ref_type=heads
 */
static constexpr uint8_t REG_CHARGE_TERMINATION = 0x05U; // Datasheet page 35: Charge end/timer cntrl
static constexpr uint8_t REG_OPERATION_CONTROL = 0x07U;  // Datasheet page 37: Misc operation control
static constexpr uint8_t REG_STATUS = 0x08U;             // Datasheet page 38: System status
static constexpr uint8_t REG_VERSION = 0x0AU;            // Datasheet page 38: Vendor/part/revision status

static constexpr TickType_t TIMEOUT = pdMS_TO_TICKS(50);

extern "C" {

extern Module bq24295_module;

static error_t read_charge_termination(Device* device, uint8_t* out) {
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    return i2c_controller_register8_get(parent, address, REG_CHARGE_TERMINATION, out, TIMEOUT);
}

error_t bq24295_get_watchdog_timer(Device* device, Bq24295WatchDogTimer* out) {
    uint8_t value;
    error_t err = read_charge_termination(device, &value);
    if (err != ERROR_NONE) {
        return err;
    }

    switch (value & 0b00110000) {
        case BQ24295_WATCHDOG_DISABLED:
            *out = BQ24295_WATCHDOG_DISABLED;
            return ERROR_NONE;
        case BQ24295_WATCHDOG_ENABLED_40S:
            *out = BQ24295_WATCHDOG_ENABLED_40S;
            return ERROR_NONE;
        case BQ24295_WATCHDOG_ENABLED_80S:
            *out = BQ24295_WATCHDOG_ENABLED_80S;
            return ERROR_NONE;
        case BQ24295_WATCHDOG_ENABLED_160S:
            *out = BQ24295_WATCHDOG_ENABLED_160S;
            return ERROR_NONE;
        default:
            return ERROR_RESOURCE;
    }
}

error_t bq24295_set_watchdog_timer(Device* device, Bq24295WatchDogTimer in) {
    uint8_t value;
    error_t err = read_charge_termination(device, &value);
    if (err != ERROR_NONE) {
        return err;
    }

    uint8_t bits_to_set = 0b00110000 & static_cast<uint8_t>(in);
    uint8_t value_cleared = value & 0b11001111;
    uint8_t to_set = bits_to_set | value_cleared;
    LOG_I(TAG, "WatchDogTimer: %02X -> %02X", value, to_set);

    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    return i2c_controller_register8_set(parent, address, REG_CHARGE_TERMINATION, to_set, TIMEOUT);
}

error_t bq24295_set_bat_fet_on(Device* device, bool on) {
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;

    // bit 5 low means bat fet is on, bit 5 high means bat fet is off
    if (on) {
        return i2c_controller_register8_reset_bits(parent, address, REG_OPERATION_CONTROL, 1 << 5, TIMEOUT);
    } else {
        return i2c_controller_register8_set_bits(parent, address, REG_OPERATION_CONTROL, 1 << 5, TIMEOUT);
    }
}

error_t bq24295_get_status(Device* device, uint8_t* value) {
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    return i2c_controller_register8_get(parent, address, REG_STATUS, value, TIMEOUT);
}

error_t bq24295_is_usb_power_connected(Device* device, bool* connected) {
    uint8_t status;
    error_t err = bq24295_get_status(device, &status);
    if (err != ERROR_NONE) {
        return err;
    }
    *connected = (status & (1 << 2)) != 0U;
    return ERROR_NONE;
}

error_t bq24295_get_version(Device* device, uint8_t* value) {
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    return i2c_controller_register8_get(parent, address, REG_VERSION, value, TIMEOUT);
}

void bq24295_print_info(Device* device) {
    uint8_t version, status, charge_termination;
    if (bq24295_get_status(device, &status) == ERROR_NONE &&
        bq24295_get_version(device, &version) == ERROR_NONE &&
        read_charge_termination(device, &charge_termination) == ERROR_NONE) {
        LOG_I(TAG, "Version %d, status %02X, charge termination %02X", version, status, charge_termination);
    } else {
        LOG_E(TAG, "Failed to retrieve version and/or status");
    }
}

// region Power supply child device

static bool ps_supports_property(Device*, PowerSupplyProperty property) {
    return property == POWER_SUPPLY_PROP_IS_CHARGING;
}

static error_t ps_get_property(Device* device, PowerSupplyProperty property, PowerSupplyPropertyValue* out_value) {
    if (property != POWER_SUPPLY_PROP_IS_CHARGING) {
        return ERROR_NOT_SUPPORTED;
    }

    // device_get_parent() here is the bq24295 device itself (this child's parent), not the I2C bus.
    uint8_t status;
    error_t error = bq24295_get_status(device_get_parent(device), &status);
    if (error != ERROR_NONE) {
        return error;
    }

    // Datasheet REG08 bits[5:4] (CHRG_STAT): 0 = not charging, 1 = pre-charge,
    // 2 = fast charge, 3 = charge termination done. Only 1|2 count as actively charging.
    uint8_t charge_status = (status >> 4) & 0x03;
    out_value->int_value = (charge_status == 1 || charge_status == 2) ? 1 : 0;
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

static constexpr PowerSupplyApi BQ24295_POWER_SUPPLY_API = {
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

// Registered (driver_construct_add() in module.cpp) so driver_bind() has a valid ->internal,
// but never matched against a devicetree node: bq24295_driver wires it up directly by pointer.
Driver bq24295_power_supply_driver = {
    .name = "bq24295-power-supply",
    .compatible = (const char*[]) { "bq24295-power-supply", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .api = &BQ24295_POWER_SUPPLY_API,
    .device_type = &POWER_SUPPLY_TYPE,
    .owner = &bq24295_module,
    .internal = nullptr
};

struct Bq24295Internal {
    Device* power_supply_device = nullptr;
};

static error_t create_power_supply_child(Device* parent, Device*& out_child) {
    auto* child = new(std::nothrow) Device { .address = 0, .name = "bq24295-power-supply", .config = nullptr, .parent = nullptr, .internal = nullptr };
    if (child == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    error_t error = device_construct(child);
    if (error != ERROR_NONE) {
        delete child;
        return error;
    }

    device_set_parent(child, parent);
    device_set_driver(child, &bq24295_power_supply_driver);

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

// endregion

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    check(device_get_type(parent) == &I2C_CONTROLLER_TYPE);

    auto* internal = new(std::nothrow) Bq24295Internal();
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
    auto* internal = static_cast<Bq24295Internal*>(device_get_driver_data(device));
    destroy_power_supply_child(internal->power_supply_device);
    device_set_driver_data(device, nullptr);
    delete internal;
    return ERROR_NONE;
}

Driver bq24295_driver = {
    .name = "bq24295",
    .compatible = (const char*[]) { "ti,bq24295", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = nullptr,
    .device_type = nullptr,
    .owner = &bq24295_module,
    .internal = nullptr
};

}
