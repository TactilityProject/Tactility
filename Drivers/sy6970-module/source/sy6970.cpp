// SPDX-License-Identifier: Apache-2.0
#include <drivers/sy6970.h>
#include <sy6970_module.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/log.h>

#include <new>

#define TAG "SY6970"
#define GET_CONFIG(device) (static_cast<const Sy6970Config*>((device)->config))
// The power-supply child's config pointer isn't set; it reads its settings from its parent
// (the sy6970 device) instead - same convention as TactilityKernel's battery_sense.cpp.
#define GET_PARENT_CONFIG(device) (static_cast<const Sy6970Config*>(device_get_parent(device)->config))
#define GET_I2C_BUS(device) (device_get_parent(device_get_parent(device)))

static const TickType_t I2C_TIMEOUT = pdMS_TO_TICKS(50);

// Register map is BQ25896-compatible for everything used here (see the vendor factory firmware's
// XPowersLib SY6970 init).
static constexpr uint8_t SY6970_REG_03 = 0x03; // bit4 CHG_CONFIG: charge enable
static constexpr uint8_t SY6970_REG_04 = 0x04; // bits[6:0] ICHG: charge current, 64 mA/LSB
static constexpr uint8_t SY6970_REG_06 = 0x06; // bits[7:2] VREG: charge voltage, 3840 mV + n*16 mV
static constexpr uint8_t SY6970_REG_07 = 0x07; // bits[5:4] WATCHDOG: I2C watchdog timer
static constexpr uint8_t SY6970_REG_09 = 0x09;
static constexpr uint8_t SY6970_REG_0B = 0x0B; // read-only status
static constexpr uint8_t SY6970_CHG_CONFIG = 1 << 4;
static constexpr uint8_t SY6970_BATFET_DIS = 1 << 5; // Force BATFET off = ship mode

// The I2C watchdog must be disabled first, otherwise the chip reverts custom register values to
// defaults after the watchdog expires. Input current limit (REG00) is left at the hardware
// PSEL/ILIM default. Non-fatal: charging still works on chip defaults if any write fails.
static bool configure_charger(Device* i2c, uint8_t address, uint32_t charge_voltage_mv, uint32_t charge_current_ma) {
    if (i2c_controller_register8_reset_bits(i2c, address, SY6970_REG_07, 0x30, I2C_TIMEOUT) != ERROR_NONE) {
        LOG_E(TAG, "Failed to disable SY6970 watchdog");
        return false;
    }

    uint8_t vreg_n = static_cast<uint8_t>((charge_voltage_mv - 3840) / 16);
    uint8_t reg06 = 0;
    if (i2c_controller_register8_get(i2c, address, SY6970_REG_06, &reg06, I2C_TIMEOUT) != ERROR_NONE ||
        i2c_controller_register8_set(i2c, address, SY6970_REG_06, static_cast<uint8_t>((reg06 & 0x03) | (vreg_n << 2)), I2C_TIMEOUT) != ERROR_NONE) {
        LOG_E(TAG, "Failed to set SY6970 charge voltage");
        return false;
    }

    uint8_t ichg_n = static_cast<uint8_t>(charge_current_ma / 64);
    uint8_t reg04 = 0;
    if (i2c_controller_register8_get(i2c, address, SY6970_REG_04, &reg04, I2C_TIMEOUT) != ERROR_NONE ||
        i2c_controller_register8_set(i2c, address, SY6970_REG_04, static_cast<uint8_t>((reg04 & 0x80) | ichg_n), I2C_TIMEOUT) != ERROR_NONE) {
        LOG_E(TAG, "Failed to set SY6970 charge current");
        return false;
    }

    LOG_I(TAG, "SY6970 configured: %lu mV / %lu mA, watchdog off", (unsigned long)charge_voltage_mv, (unsigned long)charge_current_ma);
    return true;
}

extern "C" {

// region PowerSupplyApi (power-supply child device)

static bool supports_property(Device*, PowerSupplyProperty property) {
    // Battery current/voltage/charge-level aren't available from the SY6970 alone - they come
    // from a separate fuel gauge, if the board has one.
    return property == POWER_SUPPLY_PROP_IS_CHARGING;
}

static error_t get_property(Device* device, PowerSupplyProperty property, PowerSupplyPropertyValue* out_value) {
    if (property != POWER_SUPPLY_PROP_IS_CHARGING) {
        return ERROR_NOT_SUPPORTED;
    }

    auto* i2c = GET_I2C_BUS(device);
    const auto* config = GET_PARENT_CONFIG(device);

    // REG0B CHRG_STAT bits[4:3]: 0 = not charging, 1 = pre-charge, 2 = fast charge, 3 = charge
    // done. Only 1|2 count as charging - "done" must not.
    uint8_t reg0b = 0;
    if (i2c_controller_register8_get(i2c, config->address, SY6970_REG_0B, &reg0b, I2C_TIMEOUT) != ERROR_NONE) {
        return ERROR_NOT_FOUND;
    }
    uint8_t charge_status = (reg0b >> 3) & 0x03;
    out_value->int_value = (charge_status == 1 || charge_status == 2) ? 1 : 0;
    return ERROR_NONE;
}

static bool supports_charge_control(Device*) {
    return true;
}

static bool is_allowed_to_charge(Device* device) {
    auto* i2c = GET_I2C_BUS(device);
    const auto* config = GET_PARENT_CONFIG(device);
    uint8_t reg03 = 0;
    if (i2c_controller_register8_get(i2c, config->address, SY6970_REG_03, &reg03, I2C_TIMEOUT) != ERROR_NONE) {
        return false;
    }
    return (reg03 & SY6970_CHG_CONFIG) != 0;
}

static error_t set_allowed_to_charge(Device* device, bool allowed) {
    auto* i2c = GET_I2C_BUS(device);
    const auto* config = GET_PARENT_CONFIG(device);
    return allowed
        ? i2c_controller_register8_set_bits(i2c, config->address, SY6970_REG_03, SY6970_CHG_CONFIG, I2C_TIMEOUT)
        : i2c_controller_register8_reset_bits(i2c, config->address, SY6970_REG_03, SY6970_CHG_CONFIG, I2C_TIMEOUT);
}

// Quick charge (HVDCP/PD boost) is not implemented: the ported register set doesn't cover it.
static bool supports_quick_charge(Device*) { return false; }
static bool is_quick_charge_enabled(Device*) { return false; }
static error_t set_quick_charge_enabled(Device*, bool) { return ERROR_NOT_SUPPORTED; }

static bool supports_power_off(Device*) {
    return true;
}

static error_t power_off(Device* device) {
    // Ship mode: force the charger's BATFET off (REG09 bit5). Mirrors the vendor XPowersLib
    // PowersSY6970::shutdown(). Note: this only fully powers the board down when running on
    // battery with USB unplugged.
    auto* i2c = GET_I2C_BUS(device);
    const auto* config = GET_PARENT_CONFIG(device);
    LOG_I(TAG, "Power off (SY6970 BATFET_DIS)");
    return i2c_controller_register8_set_bits(i2c, config->address, SY6970_REG_09, SY6970_BATFET_DIS, I2C_TIMEOUT);
}

static constexpr PowerSupplyApi SY6970_POWER_SUPPLY_API = {
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
// never matched against a devicetree node: sy6970_driver wires it up directly by pointer.
Driver sy6970_power_supply_driver = {
    .name = "sy6970-power-supply",
    .compatible = (const char*[]) { "sy6970-power-supply", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .api = &SY6970_POWER_SUPPLY_API,
    .device_type = &POWER_SUPPLY_TYPE,
    .owner = &sy6970_module,
    .internal = nullptr
};

// endregion

// region Driver lifecycle (sy6970 device itself)

struct Sy6970Internal {
    Device* power_supply_device = nullptr;
};

static error_t create_power_supply_child(Device* parent, Device*& out_child) {
    auto* child = new (std::nothrow) Device { .address = 0, .name = "sy6970-power-supply", .config = nullptr, .parent = nullptr, .internal = nullptr };
    if (child == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    error_t error = device_construct(child);
    if (error != ERROR_NONE) {
        delete child;
        return error;
    }

    device_set_parent(child, parent);
    device_set_driver(child, &sy6970_power_supply_driver);

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
    configure_charger(parent, config->address, config->charge_voltage_mv, config->charge_current_ma);

    auto* internal = new (std::nothrow) Sy6970Internal();
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
    auto* internal = static_cast<Sy6970Internal*>(device_get_driver_data(device));
    destroy_power_supply_child(internal->power_supply_device);
    device_set_driver_data(device, nullptr);
    delete internal;
    return ERROR_NONE;
}

Driver sy6970_driver = {
    .name = "sy6970",
    .compatible = (const char*[]) { "silergy,sy6970", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = nullptr,
    .device_type = nullptr,
    .owner = &sy6970_module,
    .internal = nullptr
};

// endregion

}
