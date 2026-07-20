// SPDX-License-Identifier: Apache-2.0
#include <drivers/ina226.h>
#include <ina226_module.h>
#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/log.h>

#include <cstdlib>
#include <new>

constexpr auto* TAG = "INA226";

// ---------------------------------------------------------------------------
// Register map
// ---------------------------------------------------------------------------
static constexpr uint8_t REG_CONFIG      = 0x00; ///< RW - Configuration
static constexpr uint8_t REG_SHUNT_VOLT  = 0x01; ///< R  - Shunt voltage (2.5 µV/LSB, signed)
static constexpr uint8_t REG_BUS_VOLT    = 0x02; ///< R  - Bus voltage (1.25 mV/LSB)
static constexpr uint8_t REG_CURRENT     = 0x04; ///< R  - Current (CURRENT_LSB/LSB, signed; valid after calibration)
static constexpr uint8_t REG_CALIBRATION = 0x05; ///< RW - Calibration

static constexpr uint8_t REG_MANUFACTURER_ID = 0xFE;
static constexpr uint16_t EXPECTED_MFR_ID    = 0x5449; // "TI"

// CONFIG: AVG=16 (010), VBUSCT=1100µs (100), VSHCT=1100µs (100), MODE=continuous shunt+bus (111)
static constexpr uint16_t CONFIG_VALUE = 0x4527;
static constexpr uint16_t CONFIG_SHUTDOWN = 0x4520; // Same as CONFIG_VALUE but MODE=000

static constexpr float MAX_CURRENT_A = 8.192f;
static constexpr float CURRENT_LSB   = MAX_CURRENT_A / 32768.0f;

static constexpr TickType_t TIMEOUT = pdMS_TO_TICKS(50);

#define GET_CONFIG(device) (static_cast<const Ina226Config*>((device)->config))

struct Ina226Internal {
    Device* power_supply_device;
};

// ---------------------------------------------------------------------------
// Power supply
// ---------------------------------------------------------------------------

static bool ps_supports_property(Device*, PowerSupplyProperty property) {
    return property == POWER_SUPPLY_PROP_VOLTAGE || property == POWER_SUPPLY_PROP_CAPACITY;
}

// battery_mv <= min_mv -> 0%, battery_mv >= reference_mv -> 100%, linear in between. min_mv is
// configurable (power-supply-min-voltage-mv) since it depends on the pack's cell count - a 1S
// LiPo's empty floor is very different from e.g. a 2S pack's.
static int estimate_capacity_from_mv(int battery_mv, uint32_t min_mv, uint32_t reference_mv) {
    if ((uint32_t)battery_mv <= min_mv) return 0;
    if ((uint32_t)battery_mv >= reference_mv) return 100;
    return (battery_mv - (int)min_mv) * 100 / ((int)reference_mv - (int)min_mv);
}

static error_t ps_get_property(Device* device, PowerSupplyProperty property, PowerSupplyPropertyValue* out_value) {
    if (property != POWER_SUPPLY_PROP_VOLTAGE && property != POWER_SUPPLY_PROP_CAPACITY) {
        return ERROR_NOT_SUPPORTED;
    }

    auto* parent = device_get_parent(device);

    float volts;
    error_t error = ina226_read_bus_voltage(parent, &volts);
    if (error != ERROR_NONE) {
        return error;
    }
    int battery_mv = static_cast<int>(volts * 1000.0f);

    const auto* parent_config = GET_CONFIG(parent);
    out_value->int_value = (property == POWER_SUPPLY_PROP_VOLTAGE)
        ? battery_mv
        : estimate_capacity_from_mv(battery_mv, parent_config->power_supply_min_voltage_mv, parent_config->power_supply_reference_voltage_mv);
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

static constexpr PowerSupplyApi INA226_POWER_SUPPLY_API = {
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
// but never matched against a devicetree node: ina226 wires it up directly by pointer.
Driver ina226_power_supply_driver = {
    .name = "ina226-power-supply",
    .compatible = (const char*[]) { "ina226-power-supply", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .api = &INA226_POWER_SUPPLY_API,
    .device_type = &POWER_SUPPLY_TYPE,
    .owner = &ina226_module,
    .internal = nullptr
};

static error_t create_power_supply_child(Device* parent, Device*& out_child) {
    auto* child = new(std::nothrow) Device { .address = 0, .name = "ina226-power-supply", .config = nullptr, .parent = nullptr, .internal = nullptr };
    if (child == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    error_t error = device_construct(child);
    if (error != ERROR_NONE) {
        delete child;
        return error;
    }

    device_set_parent(child, parent);
    device_set_driver(child, &ina226_power_supply_driver);

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
// Driver lifecycle
// ---------------------------------------------------------------------------

static error_t start(Device* device) {
    Device* i2c = device_get_parent(device);
    if (device_get_type(i2c) != &I2C_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent is not an I2C controller");
        return ERROR_RESOURCE;
    }

    const uint8_t addr = GET_CONFIG(device)->address;

    uint16_t mfr_id = 0;
    error_t err = i2c_controller_register16be_get(i2c, addr, REG_MANUFACTURER_ID, &mfr_id, TIMEOUT);
    if (err != ERROR_NONE) {
        LOG_E(TAG, "Failed to read INA226 manufacturer ID");
        return ERROR_RESOURCE;
    }

    if (mfr_id != EXPECTED_MFR_ID) {
        LOG_E(TAG, "Wrong device detected (mfr_id=0x%04X, expected=0x%04X)", mfr_id, EXPECTED_MFR_ID);
         return ERROR_RESOURCE;
     }

    if (i2c_controller_register16be_set(i2c, addr, REG_CONFIG, CONFIG_VALUE, TIMEOUT) != ERROR_NONE) {
        LOG_E(TAG, "Failed to configure INA226");
        return ERROR_RESOURCE;
    }

    const uint16_t shunt_milliohms = GET_CONFIG(device)->shunt_milliohms;
    if (shunt_milliohms == 0) {
        LOG_E(TAG, "Invalid shunt value: 0 mOhms");
        return ERROR_INVALID_ARGUMENT;
    }
    const float shunt_ohms = shunt_milliohms / 1000.0f;
    const float cal_f = 0.00512f / (CURRENT_LSB * shunt_ohms);
    if (cal_f < 1.0f || cal_f > 65535.0f) {
        LOG_E(TAG, "Calibration out of range (shunt=%u mOhms)", shunt_milliohms);
        return ERROR_INVALID_ARGUMENT;
    }
    const uint16_t cal_value = static_cast<uint16_t>(cal_f);
    if (i2c_controller_register16be_set(i2c, addr, REG_CALIBRATION, cal_value, TIMEOUT) != ERROR_NONE) {
        LOG_E(TAG, "Failed to calibrate INA226");
        return ERROR_RESOURCE;
    }

    auto* internal = static_cast<Ina226Internal*>(malloc(sizeof(Ina226Internal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    internal->power_supply_device = nullptr;
    device_set_driver_data(device, internal);

    if (GET_CONFIG(device)->power_supply) {
        error_t error = create_power_supply_child(device, internal->power_supply_device);
        if (error != ERROR_NONE) {
            LOG_E(TAG, "Failed to create power-supply device");
            free(internal);
            device_set_driver_data(device, nullptr);
            return error;
        }
    }

    LOG_I(TAG, "INA226 started (addr=0x%02X, shunt=%umΩ, cal=0x%04X)", addr, GET_CONFIG(device)->shunt_milliohms, cal_value);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* i2c = device_get_parent(device);
    if (device_get_type(i2c) != &I2C_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent is not an I2C controller");
        return ERROR_RESOURCE;
    }

    auto* internal = static_cast<Ina226Internal*>(device_get_driver_data(device));
    if (internal->power_supply_device != nullptr) {
        destroy_power_supply_child(internal->power_supply_device);
        internal->power_supply_device = nullptr;
    }

    auto addr = GET_CONFIG(device)->address;

    // Put device into shutdown mode to save power
    if (i2c_controller_register16be_set(i2c, addr, REG_CONFIG, CONFIG_SHUTDOWN, TIMEOUT) != ERROR_NONE) {
        LOG_E(TAG, "Failed to shutdown INA226 (ignored)");
    }

    free(internal);
    device_set_driver_data(device, nullptr);

    return ERROR_NONE;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

extern "C" {

error_t ina226_read_bus_voltage(Device* device, float* volts) {
    if (device == nullptr) return ERROR_INVALID_ARGUMENT;
    if (volts == nullptr) return ERROR_INVALID_ARGUMENT;
    uint16_t raw = 0;
    error_t err = i2c_controller_register16be_get(device_get_parent(device), GET_CONFIG(device)->address, REG_BUS_VOLT, &raw, TIMEOUT);
    if (err != ERROR_NONE) return err;
    *volts = static_cast<float>(raw) * 0.00125f;
    return ERROR_NONE;
}

error_t ina226_read_shunt_current(Device* device, float* amps) {
    if (device == nullptr) return ERROR_INVALID_ARGUMENT;
    if (amps == nullptr) return ERROR_INVALID_ARGUMENT;
    uint16_t raw = 0;
    error_t err = i2c_controller_register16be_get(device_get_parent(device), GET_CONFIG(device)->address, REG_CURRENT, &raw, TIMEOUT);
    if (err != ERROR_NONE) return err;
    *amps = static_cast<float>(static_cast<int16_t>(raw)) * CURRENT_LSB;
    return ERROR_NONE;
}

Driver ina226_driver = {
    .name = "ina226",
    .compatible = (const char*[]) { "ti,ina226", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = nullptr,
    .device_type = nullptr,
    .owner = &ina226_module,
    .internal = nullptr
};

} // extern "C"
