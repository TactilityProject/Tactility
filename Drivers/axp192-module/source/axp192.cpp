// SPDX-License-Identifier: Apache-2.0
#include <drivers/axp192.h>
#include <axp192_module.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/log.h>

#include <new>

#define GET_CONFIG(device) (static_cast<const Axp192Config*>((device)->config))

/** Reference: https://github.com/tuupola/axp192 (register map and ADC/voltage formulas) */
static constexpr uint8_t REG_MODE_CHGSTATUS = 0x01U;               // bit6: battery is charging
static constexpr uint8_t REG_EXTEN_DCDC2_CONTROL = 0x10U;          // bit2: EXTEN, bit0: DCDC2
static constexpr uint8_t REG_DCDC13_LDO23_CONTROL = 0x12U;         // bit3: LDO3, bit2: LDO2, bit1: DCDC3, bit0: DCDC1
static constexpr uint8_t REG_DCDC2_VOLTAGE = 0x23U;
static constexpr uint8_t REG_DCDC1_VOLTAGE = 0x26U;
static constexpr uint8_t REG_DCDC3_VOLTAGE = 0x27U;
static constexpr uint8_t REG_LDO23_VOLTAGE = 0x28U;                // bits7-4: LDO2, bits3-0: LDO3
static constexpr uint8_t REG_SHUTDOWN_BATTERY_CHGLED_CONTROL = 0x32U; // bit7: power off
static constexpr uint8_t REG_CHARGE_CONTROL_1 = 0x33U;             // bit7: charging enabled
static constexpr uint8_t REG_BATTERY_POWER = 0x70U;                // 3 bytes, 0.55uW/LSB
static constexpr uint8_t REG_BATTERY_VOLTAGE = 0x78U;              // 2 bytes, 1.1mV/LSB
static constexpr uint8_t REG_CHARGE_CURRENT = 0x7AU;               // 2 bytes, 0.5mA/LSB
static constexpr uint8_t REG_DISCHARGE_CURRENT = 0x7CU;            // 2 bytes, 0.5mA/LSB
static constexpr uint8_t REG_GPIO1_CONTROL = 0x92U;                // function select (0x02 = PWM1 output)
static constexpr uint8_t REG_PWM1_DUTY_CYCLE_2 = 0x9AU;            // PWM1 duty cycle, low byte

static constexpr uint8_t GPIO1_FUNCTION_PWM1 = 0x02U;

static constexpr uint8_t BIT_CHARGING = 1U << 6U;
static constexpr uint8_t BIT_CHARGE_ENABLED = 1U << 7U;
static constexpr uint8_t BIT_POWER_OFF = 1U << 7U;

static constexpr TickType_t TIMEOUT = pdMS_TO_TICKS(50);

extern "C" {

extern Module axp192_module;

// region Register helpers

/** Reads a 2-byte ADC register in AXP192's packed 12-bit format: raw = (high << 4) + low. */
static error_t read_adc_raw(Device* device, uint8_t reg, uint16_t* raw) {
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    uint8_t buffer[2];
    error_t err = i2c_controller_read_register(parent, address, reg, buffer, sizeof(buffer), TIMEOUT);
    if (err != ERROR_NONE) {
        return err;
    }
    *raw = static_cast<uint16_t>((buffer[0] << 4U) + (buffer[1] & 0x0FU));
    return ERROR_NONE;
}

static error_t get_rail_enable_bit(Axp192Rail rail, uint8_t* reg, uint8_t* bit) {
    switch (rail) {
        case AXP192_RAIL_DCDC1:
            *reg = REG_DCDC13_LDO23_CONTROL;
            *bit = 1U << 0U;
            return ERROR_NONE;
        case AXP192_RAIL_DCDC2:
            *reg = REG_EXTEN_DCDC2_CONTROL;
            *bit = 1U << 0U;
            return ERROR_NONE;
        case AXP192_RAIL_DCDC3:
            *reg = REG_DCDC13_LDO23_CONTROL;
            *bit = 1U << 1U;
            return ERROR_NONE;
        case AXP192_RAIL_LDO2:
            *reg = REG_DCDC13_LDO23_CONTROL;
            *bit = 1U << 2U;
            return ERROR_NONE;
        case AXP192_RAIL_LDO3:
            *reg = REG_DCDC13_LDO23_CONTROL;
            *bit = 1U << 3U;
            return ERROR_NONE;
        case AXP192_RAIL_EXTEN:
            *reg = REG_EXTEN_DCDC2_CONTROL;
            *bit = 1U << 2U;
            return ERROR_NONE;
    }
    return ERROR_INVALID_ARGUMENT;
}

// endregion

error_t axp192_is_rail_enabled(Device* device, Axp192Rail rail, bool* enabled) {
    uint8_t reg, bit;
    error_t err = get_rail_enable_bit(rail, &reg, &bit);
    if (err != ERROR_NONE) {
        return err;
    }

    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    uint8_t value;
    err = i2c_controller_register8_get(parent, address, reg, &value, TIMEOUT);
    if (err != ERROR_NONE) {
        return err;
    }

    *enabled = (value & bit) != 0U;
    return ERROR_NONE;
}

error_t axp192_set_rail_enabled(Device* device, Axp192Rail rail, bool enabled) {
    uint8_t reg, bit;
    error_t err = get_rail_enable_bit(rail, &reg, &bit);
    if (err != ERROR_NONE) {
        return err;
    }

    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    if (enabled) {
        return i2c_controller_register8_set_bits(parent, address, reg, bit, TIMEOUT);
    } else {
        return i2c_controller_register8_reset_bits(parent, address, reg, bit, TIMEOUT);
    }
}

error_t axp192_set_rail_voltage(Device* device, Axp192Rail rail, uint16_t millivolts) {
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;

    switch (rail) {
        case AXP192_RAIL_DCDC1:
        case AXP192_RAIL_DCDC3: {
            if (millivolts < 700U || millivolts > 3500U) {
                return ERROR_INVALID_ARGUMENT;
            }
            uint8_t reg = (rail == AXP192_RAIL_DCDC1) ? REG_DCDC1_VOLTAGE : REG_DCDC3_VOLTAGE;
            uint8_t value = static_cast<uint8_t>((millivolts - 700U) / 25U);
            return i2c_controller_register8_set(parent, address, reg, value, TIMEOUT);
        }
        case AXP192_RAIL_DCDC2: {
            if (millivolts < 700U || millivolts > 2275U) {
                return ERROR_INVALID_ARGUMENT;
            }
            uint8_t value = static_cast<uint8_t>((millivolts - 700U) / 25U);
            return i2c_controller_register8_set(parent, address, REG_DCDC2_VOLTAGE, value, TIMEOUT);
        }
        case AXP192_RAIL_LDO2:
        case AXP192_RAIL_LDO3: {
            if (millivolts < 1800U || millivolts > 3300U) {
                return ERROR_INVALID_ARGUMENT;
            }
            uint8_t step = static_cast<uint8_t>((millivolts - 1800U) / 100U);
            uint8_t value;
            error_t err = i2c_controller_register8_get(parent, address, REG_LDO23_VOLTAGE, &value, TIMEOUT);
            if (err != ERROR_NONE) {
                return err;
            }
            if (rail == AXP192_RAIL_LDO2) {
                value = static_cast<uint8_t>((value & 0x0FU) | (step << 4U));
            } else {
                value = static_cast<uint8_t>((value & 0xF0U) | step);
            }
            return i2c_controller_register8_set(parent, address, REG_LDO23_VOLTAGE, value, TIMEOUT);
        }
        case AXP192_RAIL_EXTEN:
            return ERROR_NOT_SUPPORTED;
    }
    return ERROR_INVALID_ARGUMENT;
}

error_t axp192_get_battery_voltage(Device* device, uint16_t* millivolts) {
    uint16_t raw;
    error_t err = read_adc_raw(device, REG_BATTERY_VOLTAGE, &raw);
    if (err != ERROR_NONE) {
        return err;
    }
    *millivolts = static_cast<uint16_t>((raw * 11U) / 10U); // 1.1mV/LSB
    return ERROR_NONE;
}

error_t axp192_get_battery_charge_current(Device* device, uint16_t* milliamps) {
    uint16_t raw;
    error_t err = read_adc_raw(device, REG_CHARGE_CURRENT, &raw);
    if (err != ERROR_NONE) {
        return err;
    }
    *milliamps = raw / 2U; // 0.5mA/LSB
    return ERROR_NONE;
}

error_t axp192_get_battery_discharge_current(Device* device, uint16_t* milliamps) {
    uint16_t raw;
    error_t err = read_adc_raw(device, REG_DISCHARGE_CURRENT, &raw);
    if (err != ERROR_NONE) {
        return err;
    }
    *milliamps = raw / 2U; // 0.5mA/LSB
    return ERROR_NONE;
}

error_t axp192_get_battery_power(Device* device, uint32_t* microwatts) {
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    uint8_t buffer[3];
    error_t err = i2c_controller_read_register(parent, address, REG_BATTERY_POWER, buffer, sizeof(buffer), TIMEOUT);
    if (err != ERROR_NONE) {
        return err;
    }
    uint32_t raw = (static_cast<uint32_t>(buffer[0]) << 16U) | (static_cast<uint32_t>(buffer[1]) << 8U) | buffer[2];
    *microwatts = (raw * 11U) / 20U; // 1.1mV * 0.5mA = 0.55uW/LSB
    return ERROR_NONE;
}

error_t axp192_is_charging(Device* device, bool* charging) {
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    uint8_t value;
    error_t err = i2c_controller_register8_get(parent, address, REG_MODE_CHGSTATUS, &value, TIMEOUT);
    if (err != ERROR_NONE) {
        return err;
    }
    *charging = (value & BIT_CHARGING) != 0U;
    return ERROR_NONE;
}

error_t axp192_is_charge_enabled(Device* device, bool* enabled) {
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    uint8_t value;
    error_t err = i2c_controller_register8_get(parent, address, REG_CHARGE_CONTROL_1, &value, TIMEOUT);
    if (err != ERROR_NONE) {
        return err;
    }
    *enabled = (value & BIT_CHARGE_ENABLED) != 0U;
    return ERROR_NONE;
}

error_t axp192_set_charge_enabled(Device* device, bool enabled) {
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    if (enabled) {
        return i2c_controller_register8_set_bits(parent, address, REG_CHARGE_CONTROL_1, BIT_CHARGE_ENABLED, TIMEOUT);
    } else {
        return i2c_controller_register8_reset_bits(parent, address, REG_CHARGE_CONTROL_1, BIT_CHARGE_ENABLED, TIMEOUT);
    }
}

error_t axp192_power_off(Device* device) {
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    return i2c_controller_register8_set_bits(parent, address, REG_SHUTDOWN_BATTERY_CHGLED_CONTROL, BIT_POWER_OFF, TIMEOUT);
}

error_t axp192_set_gpio1_pwm1_output(Device* device) {
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    return i2c_controller_register8_set(parent, address, REG_GPIO1_CONTROL, GPIO1_FUNCTION_PWM1, TIMEOUT);
}

error_t axp192_set_pwm1_duty_cycle(Device* device, uint8_t duty) {
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    return i2c_controller_register8_set(parent, address, REG_PWM1_DUTY_CYCLE_2, duty, TIMEOUT);
}

// region Power supply child device

static bool ps_supports_property(Device*, PowerSupplyProperty property) {
    return property == POWER_SUPPLY_PROP_IS_CHARGING ||
        property == POWER_SUPPLY_PROP_VOLTAGE ||
        property == POWER_SUPPLY_PROP_CURRENT;
}

static error_t ps_get_property(Device* device, PowerSupplyProperty property, PowerSupplyPropertyValue* out_value) {
    // device_get_parent() here is the axp192 device itself (this child's parent), not the I2C bus.
    auto* axp192_device = device_get_parent(device);

    switch (property) {
        case POWER_SUPPLY_PROP_IS_CHARGING: {
            bool charging;
            error_t err = axp192_is_charging(axp192_device, &charging);
            if (err != ERROR_NONE) {
                return err;
            }
            out_value->int_value = charging ? 1 : 0;
            return ERROR_NONE;
        }
        case POWER_SUPPLY_PROP_VOLTAGE: {
            uint16_t millivolts;
            error_t err = axp192_get_battery_voltage(axp192_device, &millivolts);
            if (err != ERROR_NONE) {
                return err;
            }
            out_value->int_value = millivolts;
            return ERROR_NONE;
        }
        case POWER_SUPPLY_PROP_CURRENT: {
            uint16_t charge_current, discharge_current;
            error_t err = axp192_get_battery_charge_current(axp192_device, &charge_current);
            if (err != ERROR_NONE) {
                return err;
            }
            err = axp192_get_battery_discharge_current(axp192_device, &discharge_current);
            if (err != ERROR_NONE) {
                return err;
            }
            out_value->int_value = (charge_current > 0U) ? charge_current : -static_cast<int>(discharge_current);
            return ERROR_NONE;
        }
        default:
            return ERROR_NOT_SUPPORTED;
    }
}

static bool ps_supports_charge_control(Device*) { return true; }

static bool ps_is_allowed_to_charge(Device* device) {
    bool enabled = false;
    axp192_is_charge_enabled(device_get_parent(device), &enabled);
    return enabled;
}

static error_t ps_set_allowed_to_charge(Device* device, bool allowed) {
    return axp192_set_charge_enabled(device_get_parent(device), allowed);
}

static bool ps_supports_quick_charge(Device*) { return false; }
static bool ps_is_quick_charge_enabled(Device*) { return false; }
static error_t ps_set_quick_charge_enabled(Device*, bool) { return ERROR_NOT_SUPPORTED; }
static bool ps_supports_power_off(Device*) { return true; }
static error_t ps_power_off(Device* device) { return axp192_power_off(device_get_parent(device)); }

static constexpr PowerSupplyApi AXP192_POWER_SUPPLY_API = {
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
// but never matched against a devicetree node: axp192_driver wires it up directly by pointer.
Driver axp192_power_supply_driver = {
    .name = "axp192-power-supply",
    .compatible = (const char*[]) { "axp192-power-supply", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .api = &AXP192_POWER_SUPPLY_API,
    .device_type = &POWER_SUPPLY_TYPE,
    .owner = &axp192_module,
    .internal = nullptr
};

struct Axp192Internal {
    Device* power_supply_device = nullptr;
};

static error_t create_power_supply_child(Device* parent, Device*& out_child) {
    auto* child = new(std::nothrow) Device { .address = 0, .name = "axp192-power-supply", .config = nullptr, .parent = nullptr, .internal = nullptr };
    if (child == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    error_t error = device_construct(child);
    if (error != ERROR_NONE) {
        delete child;
        return error;
    }

    device_set_parent(child, parent);
    device_set_driver(child, &axp192_power_supply_driver);

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

    auto* internal = new(std::nothrow) Axp192Internal();
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
    auto* internal = static_cast<Axp192Internal*>(device_get_driver_data(device));
    destroy_power_supply_child(internal->power_supply_device);
    device_set_driver_data(device, nullptr);
    delete internal;
    return ERROR_NONE;
}

Driver axp192_driver = {
    .name = "axp192",
    .compatible = (const char*[]) { "x-powers,axp192", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = nullptr,
    .device_type = nullptr,
    .owner = &axp192_module,
    .internal = nullptr
};

}
