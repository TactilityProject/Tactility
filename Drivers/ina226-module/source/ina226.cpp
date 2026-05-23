// SPDX-License-Identifier: Apache-2.0
#include <drivers/ina226.h>
#include <ina226_module.h>
#include <tactility/device.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/log.h>

#define TAG "INA226"

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

    const float shunt_ohms = GET_CONFIG(device)->shunt_milliohms / 1000.0f;
    const uint16_t cal_value = static_cast<uint16_t>(0.00512f / (CURRENT_LSB * shunt_ohms));
    if (i2c_controller_register16be_set(i2c, addr, REG_CALIBRATION, cal_value, TIMEOUT) != ERROR_NONE) {
        LOG_E(TAG, "Failed to calibrate INA226");
        return ERROR_RESOURCE;
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

    auto addr = GET_CONFIG(device)->address;

    // Put device into shutdown mode to save power
    if (i2c_controller_register16be_set(i2c, addr, REG_CONFIG, CONFIG_SHUTDOWN, TIMEOUT) != ERROR_NONE) {
        LOG_E(TAG, "Failed to shutdown INA226 (ignored)");
    }

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
