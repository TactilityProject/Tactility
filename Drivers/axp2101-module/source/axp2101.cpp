// SPDX-License-Identifier: Apache-2.0
#include <drivers/axp2101.h>
#include <axp2101_module.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/log.h>

#include <new>

#define TAG "AXP2101"
#define GET_CONFIG(device) (static_cast<const Axp2101Config*>((device)->config))

/** Reference: https://github.com/lewisxhe/XPowersLib (AXP2101 register map and voltage-encoding formulas) */
static constexpr uint8_t REG_STATUS1 = 0x00U;              // bit5: VBUS good
static constexpr uint8_t REG_STATUS2 = 0x01U;               // bits[6:5]: charge status (1=charging), bit3: battery connected(?)
static constexpr uint8_t REG_COMMON_CONFIG = 0x10U;          // bit0: shutdown
static constexpr uint8_t REG_CHARGE_GAUGE_WDT_CTRL = 0x18U;  // bit1: charge enable
static constexpr uint8_t REG_ADC_DATA_RESULT0 = 0x34U;       // battery voltage, high 5 bits
static constexpr uint8_t REG_ADC_DATA_RESULT1 = 0x35U;       // battery voltage, low 8 bits
static constexpr uint8_t REG_ADC_DATA_RESULT4 = 0x38U;       // VBUS voltage, high 6 bits
static constexpr uint8_t REG_ADC_DATA_RESULT5 = 0x39U;       // VBUS voltage, low 8 bits
static constexpr uint8_t REG_DC_ONOFF_DVM_CTRL = 0x80U;      // bits0-4: DCDC1-5 enable
static constexpr uint8_t REG_DC_VOL0_CTRL = 0x82U;           // DCDC1 voltage; DCDC2-5 follow at +1..+4
static constexpr uint8_t REG_LDO_ONOFF_CTRL0 = 0x90U;        // bits0-7: ALDO1-4, BLDO1-2, CPUSLDO, DLDO1 enable
static constexpr uint8_t REG_LDO_ONOFF_CTRL1 = 0x91U;        // bit0: DLDO2 enable
static constexpr uint8_t REG_LDO_VOL0_CTRL = 0x92U;          // ALDO1 voltage; remaining LDOs follow at +1..+8
static constexpr uint8_t REG_ADC_CHANNEL_CTRL = 0x30U;       // bit0: battery voltage, bit2: VBUS voltage

static constexpr uint8_t BIT_VBUS_GOOD = 1U << 5U;
static constexpr uint8_t BIT_SHUTDOWN = 1U << 0U;
static constexpr uint8_t BIT_CHARGE_ENABLED = 1U << 1U;

static constexpr TickType_t TIMEOUT = pdMS_TO_TICKS(50);

extern "C" {

extern Module axp2101_module;

// region Voltage encoding

struct Axp2101VoltRange {
    uint16_t min;
    uint16_t max;
    uint16_t step;
    uint8_t code_base;
};

static error_t encode_ranged_voltage(uint16_t millivolts, const Axp2101VoltRange* ranges, size_t range_count, uint8_t* out_code) {
    for (size_t i = 0; i < range_count; i++) {
        const Axp2101VoltRange& range = ranges[i];
        if (millivolts >= range.min && millivolts <= range.max) {
            if ((millivolts - range.min) % range.step != 0U) {
                return ERROR_INVALID_ARGUMENT;
            }
            *out_code = static_cast<uint8_t>(range.code_base + (millivolts - range.min) / range.step);
            return ERROR_NONE;
        }
    }
    return ERROR_INVALID_ARGUMENT;
}

static error_t write_masked_register(Device* device, uint8_t reg, uint8_t preserve_mask, uint8_t code) {
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;

    if (preserve_mask == 0U) {
        return i2c_controller_register8_set(parent, address, reg, code, TIMEOUT);
    }

    uint8_t value;
    error_t err = i2c_controller_register8_get(parent, address, reg, &value, TIMEOUT);
    if (err != ERROR_NONE) {
        return err;
    }
    value = static_cast<uint8_t>((value & preserve_mask) | code);
    return i2c_controller_register8_set(parent, address, reg, value, TIMEOUT);
}

// endregion

// region DCDC

static error_t get_dcdc_enable_bit(Axp2101Dcdc dcdc, uint8_t* bit) {
    if (dcdc < AXP2101_DCDC1 || dcdc > AXP2101_DCDC5) {
        return ERROR_INVALID_ARGUMENT;
    }
    *bit = static_cast<uint8_t>(1U << dcdc);
    return ERROR_NONE;
}

error_t axp2101_is_dcdc_enabled(Device* device, Axp2101Dcdc dcdc, bool* enabled) {
    uint8_t bit;
    error_t err = get_dcdc_enable_bit(dcdc, &bit);
    if (err != ERROR_NONE) {
        return err;
    }

    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    uint8_t value;
    err = i2c_controller_register8_get(parent, address, REG_DC_ONOFF_DVM_CTRL, &value, TIMEOUT);
    if (err != ERROR_NONE) {
        return err;
    }

    *enabled = (value & bit) != 0U;
    return ERROR_NONE;
}

error_t axp2101_set_dcdc_enabled(Device* device, Axp2101Dcdc dcdc, bool enabled) {
    uint8_t bit;
    error_t err = get_dcdc_enable_bit(dcdc, &bit);
    if (err != ERROR_NONE) {
        return err;
    }

    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    if (enabled) {
        return i2c_controller_register8_set_bits(parent, address, REG_DC_ONOFF_DVM_CTRL, bit, TIMEOUT);
    } else {
        return i2c_controller_register8_reset_bits(parent, address, REG_DC_ONOFF_DVM_CTRL, bit, TIMEOUT);
    }
}

error_t axp2101_set_dcdc_voltage(Device* device, Axp2101Dcdc dcdc, uint16_t millivolts) {
    uint8_t reg = static_cast<uint8_t>(REG_DC_VOL0_CTRL + dcdc);
    uint8_t code;
    error_t err;

    switch (dcdc) {
        case AXP2101_DCDC1: {
            static constexpr Axp2101VoltRange ranges[] = { { 1500, 3400, 100, 0 } };
            err = encode_ranged_voltage(millivolts, ranges, 1, &code);
            if (err != ERROR_NONE) {
                return err;
            }
            return write_masked_register(device, reg, 0x00U, code);
        }
        case AXP2101_DCDC2: {
            static constexpr Axp2101VoltRange ranges[] = { { 500, 1200, 10, 0 }, { 1220, 1540, 20, 71 } };
            err = encode_ranged_voltage(millivolts, ranges, 2, &code);
            if (err != ERROR_NONE) {
                return err;
            }
            return write_masked_register(device, reg, 0x80U, code);
        }
        case AXP2101_DCDC3: {
            static constexpr Axp2101VoltRange ranges[] = { { 500, 1200, 10, 0 }, { 1220, 1540, 20, 71 }, { 1600, 3400, 100, 88 } };
            err = encode_ranged_voltage(millivolts, ranges, 3, &code);
            if (err != ERROR_NONE) {
                return err;
            }
            return write_masked_register(device, reg, 0x80U, code);
        }
        case AXP2101_DCDC4: {
            static constexpr Axp2101VoltRange ranges[] = { { 500, 1200, 10, 0 }, { 1220, 1840, 20, 71 } };
            err = encode_ranged_voltage(millivolts, ranges, 2, &code);
            if (err != ERROR_NONE) {
                return err;
            }
            return write_masked_register(device, reg, 0x80U, code);
        }
        case AXP2101_DCDC5: {
            // DCDC5 datasheet quirk: 1200mV maps to a fixed out-of-sequence code, distinct
            // from the linear 1400-3700mV range (see XPowersLib's setDC5Voltage()).
            if (millivolts == 1200U) {
                return write_masked_register(device, reg, 0xE0U, 0x19U);
            }
            static constexpr Axp2101VoltRange ranges[] = { { 1400, 3700, 100, 0 } };
            err = encode_ranged_voltage(millivolts, ranges, 1, &code);
            if (err != ERROR_NONE) {
                return err;
            }
            return write_masked_register(device, reg, 0xE0U, code);
        }
    }
    return ERROR_INVALID_ARGUMENT;
}

// endregion

// region LDO

static error_t get_ldo_enable_location(Axp2101Ldo ldo, uint8_t* reg, uint8_t* bit) {
    if (ldo < AXP2101_ALDO1 || ldo > AXP2101_DLDO2) {
        return ERROR_INVALID_ARGUMENT;
    }
    if (ldo == AXP2101_DLDO2) {
        *reg = REG_LDO_ONOFF_CTRL1;
        *bit = 1U << 0U;
    } else {
        *reg = REG_LDO_ONOFF_CTRL0;
        *bit = static_cast<uint8_t>(1U << ldo);
    }
    return ERROR_NONE;
}

error_t axp2101_is_ldo_enabled(Device* device, Axp2101Ldo ldo, bool* enabled) {
    uint8_t reg, bit;
    error_t err = get_ldo_enable_location(ldo, &reg, &bit);
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

error_t axp2101_set_ldo_enabled(Device* device, Axp2101Ldo ldo, bool enabled) {
    uint8_t reg, bit;
    error_t err = get_ldo_enable_location(ldo, &reg, &bit);
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

error_t axp2101_set_ldo_voltage(Device* device, Axp2101Ldo ldo, uint16_t millivolts) {
    if (ldo < AXP2101_ALDO1 || ldo > AXP2101_DLDO2) {
        return ERROR_INVALID_ARGUMENT;
    }

    static constexpr Axp2101VoltRange LDO_RANGE[] = {
        { 500, 3500, 100, 0 }, // ALDO1
        { 500, 3500, 100, 0 }, // ALDO2
        { 500, 3500, 100, 0 }, // ALDO3
        { 500, 3500, 100, 0 }, // ALDO4
        { 500, 3500, 100, 0 }, // BLDO1
        { 500, 3500, 100, 0 }, // BLDO2
        { 500, 1400, 50, 0 },  // CPUSLDO
        { 500, 3400, 100, 0 }, // DLDO1
        { 500, 3400, 100, 0 }, // DLDO2
    };

    uint8_t code;
    error_t err = encode_ranged_voltage(millivolts, &LDO_RANGE[ldo], 1, &code);
    if (err != ERROR_NONE) {
        return err;
    }

    uint8_t reg = static_cast<uint8_t>(REG_LDO_VOL0_CTRL + ldo);
    return write_masked_register(device, reg, 0xE0U, code);
}

// endregion

error_t axp2101_get_battery_voltage(Device* device, uint16_t* millivolts) {
    bool connected;
    error_t err = axp2101_is_battery_connected(device, &connected);
    if (err != ERROR_NONE) {
        return err;
    }
    if (!connected) {
        *millivolts = 0;
        return ERROR_NONE;
    }

    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    uint8_t high, low;
    err = i2c_controller_register8_get(parent, address, REG_ADC_DATA_RESULT0, &high, TIMEOUT);
    if (err != ERROR_NONE) {
        return err;
    }
    err = i2c_controller_register8_get(parent, address, REG_ADC_DATA_RESULT1, &low, TIMEOUT);
    if (err != ERROR_NONE) {
        return err;
    }
    *millivolts = static_cast<uint16_t>(((high & 0x1FU) << 8U) | low);
    return ERROR_NONE;
}

error_t axp2101_is_battery_connected(Device* device, bool* connected) {
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    uint8_t value;
    error_t err = i2c_controller_register8_get(parent, address, REG_STATUS1, &value, TIMEOUT);
    if (err != ERROR_NONE) {
        return err;
    }
    *connected = (value & (1U << 3U)) != 0U;
    return ERROR_NONE;
}

error_t axp2101_is_vbus_present(Device* device, bool* present) {
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    uint8_t value;
    error_t err = i2c_controller_register8_get(parent, address, REG_STATUS1, &value, TIMEOUT);
    if (err != ERROR_NONE) {
        return err;
    }
    *present = (value & BIT_VBUS_GOOD) != 0U;
    return ERROR_NONE;
}

error_t axp2101_get_vbus_voltage(Device* device, uint16_t* millivolts) {
    bool present;
    error_t err = axp2101_is_vbus_present(device, &present);
    if (err != ERROR_NONE) {
        return err;
    }
    if (!present) {
        *millivolts = 0;
        return ERROR_NONE;
    }

    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    uint8_t high, low;
    err = i2c_controller_register8_get(parent, address, REG_ADC_DATA_RESULT4, &high, TIMEOUT);
    if (err != ERROR_NONE) {
        return err;
    }
    err = i2c_controller_register8_get(parent, address, REG_ADC_DATA_RESULT5, &low, TIMEOUT);
    if (err != ERROR_NONE) {
        return err;
    }
    *millivolts = static_cast<uint16_t>(((high & 0x3FU) << 8U) | low);
    return ERROR_NONE;
}

error_t axp2101_is_charging(Device* device, bool* charging) {
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    uint8_t value;
    error_t err = i2c_controller_register8_get(parent, address, REG_STATUS2, &value, TIMEOUT);
    if (err != ERROR_NONE) {
        return err;
    }
    *charging = ((value >> 5U) & 0x03U) == 0x01U;
    return ERROR_NONE;
}

error_t axp2101_is_charge_enabled(Device* device, bool* enabled) {
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    uint8_t value;
    error_t err = i2c_controller_register8_get(parent, address, REG_CHARGE_GAUGE_WDT_CTRL, &value, TIMEOUT);
    if (err != ERROR_NONE) {
        return err;
    }
    *enabled = (value & BIT_CHARGE_ENABLED) != 0U;
    return ERROR_NONE;
}

error_t axp2101_set_charge_enabled(Device* device, bool enabled) {
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    if (enabled) {
        return i2c_controller_register8_set_bits(parent, address, REG_CHARGE_GAUGE_WDT_CTRL, BIT_CHARGE_ENABLED, TIMEOUT);
    } else {
        return i2c_controller_register8_reset_bits(parent, address, REG_CHARGE_GAUGE_WDT_CTRL, BIT_CHARGE_ENABLED, TIMEOUT);
    }
}

error_t axp2101_power_off(Device* device) {
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    return i2c_controller_register8_set_bits(parent, address, REG_COMMON_CONFIG, BIT_SHUTDOWN, TIMEOUT);
}

// region Power supply child device

static bool ps_supports_property(Device*, PowerSupplyProperty property) {
    return property == POWER_SUPPLY_PROP_IS_CHARGING || property == POWER_SUPPLY_PROP_VOLTAGE;
}

static error_t ps_get_property(Device* device, PowerSupplyProperty property, PowerSupplyPropertyValue* out_value) {
    // device_get_parent() here is the axp2101 device itself (this child's parent), not the I2C bus.
    auto* axp2101_device = device_get_parent(device);

    switch (property) {
        case POWER_SUPPLY_PROP_IS_CHARGING: {
            bool charging;
            error_t err = axp2101_is_charging(axp2101_device, &charging);
            if (err != ERROR_NONE) {
                return err;
            }
            out_value->int_value = charging ? 1 : 0;
            return ERROR_NONE;
        }
        case POWER_SUPPLY_PROP_VOLTAGE: {
            uint16_t millivolts;
            error_t err = axp2101_get_battery_voltage(axp2101_device, &millivolts);
            if (err != ERROR_NONE) {
                return err;
            }
            out_value->int_value = millivolts;
            return ERROR_NONE;
        }
        default:
            return ERROR_NOT_SUPPORTED;
    }
}

static bool ps_supports_charge_control(Device*) { return true; }

static bool ps_is_allowed_to_charge(Device* device) {
    bool enabled = false;
    axp2101_is_charge_enabled(device_get_parent(device), &enabled);
    return enabled;
}

static error_t ps_set_allowed_to_charge(Device* device, bool allowed) {
    return axp2101_set_charge_enabled(device_get_parent(device), allowed);
}

static bool ps_supports_quick_charge(Device*) { return false; }
static bool ps_is_quick_charge_enabled(Device*) { return false; }
static error_t ps_set_quick_charge_enabled(Device*, bool) { return ERROR_NOT_SUPPORTED; }
static bool ps_supports_power_off(Device*) { return true; }
static error_t ps_power_off(Device* device) { return axp2101_power_off(device_get_parent(device)); }

static constexpr PowerSupplyApi AXP2101_POWER_SUPPLY_API = {
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
// but never matched against a devicetree node: axp2101_driver wires it up directly by pointer.
Driver axp2101_power_supply_driver = {
    .name = "axp2101-power-supply",
    .compatible = (const char*[]) { "axp2101-power-supply", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .api = &AXP2101_POWER_SUPPLY_API,
    .device_type = &POWER_SUPPLY_TYPE,
    .owner = &axp2101_module,
    .internal = nullptr
};

struct Axp2101Internal {
    Device* power_supply_device = nullptr;
};

static error_t create_power_supply_child(Device* parent, Device*& out_child) {
    auto* child = new(std::nothrow) Device { .address = 0, .name = "axp2101-power-supply", .config = nullptr, .parent = nullptr, .internal = nullptr };
    if (child == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    error_t error = device_construct(child);
    if (error != ERROR_NONE) {
        delete child;
        return error;
    }

    device_set_parent(child, parent);
    device_set_driver(child, &axp2101_power_supply_driver);

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

    auto address = GET_CONFIG(device)->address;
    // Battery/VBUS voltage ADC channels are off by default; axp2101_get_battery_voltage()
    // and axp2101_get_vbus_voltage() need them on to read anything but 0.
    error_t error = i2c_controller_register8_set_bits(parent, address, REG_ADC_CHANNEL_CTRL, (1U << 0U) | (1U << 2U), TIMEOUT);
    if (error != ERROR_NONE) {
        LOG_W(TAG, "Failed to enable battery/VBUS ADC channels");
        return error;
    }

    auto* internal = new(std::nothrow) Axp2101Internal();
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    error = create_power_supply_child(device, internal->power_supply_device);
    if (error != ERROR_NONE) {
        delete internal;
        return error;
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Axp2101Internal*>(device_get_driver_data(device));
    destroy_power_supply_child(internal->power_supply_device);
    device_set_driver_data(device, nullptr);
    delete internal;
    return ERROR_NONE;
}

Driver axp2101_driver = {
    .name = "axp2101",
    .compatible = (const char*[]) { "x-powers,axp2101", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = nullptr,
    .device_type = nullptr,
    .owner = &axp2101_module,
    .internal = nullptr
};

}
