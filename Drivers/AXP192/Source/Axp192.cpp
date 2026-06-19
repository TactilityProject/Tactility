#include "Axp192.h"
#include <tactility/drivers/i2c_controller.h>

constexpr auto TAG = "Axp192Power";

int32_t Axp192::i2cRead(void* handle, uint8_t address, uint8_t reg, uint8_t* buffer, uint16_t size) {
    const auto* device = static_cast<Axp192*>(handle);
    if (i2c_controller_read_register(device->configuration->controller, address, reg, buffer, size, device->configuration->readTimeout) == ERROR_NONE) {
        return AXP192_OK;
    } else {
        return 1;
    }
}

int32_t Axp192::i2cWrite(void* handle, uint8_t address, uint8_t reg, const uint8_t* buffer, uint16_t size) {
    const auto* device = static_cast<Axp192*>(handle);
    if (i2c_controller_write_register(device->configuration->controller, address, reg, buffer, size, device->configuration->writeTimeout) == ERROR_NONE) {
        return AXP192_OK;
    } else {
        return 1;
    }
}

bool Axp192::supportsMetric(MetricType type) const {
    if (!isInitialized) {
        return false;
    }

    switch (type) {
        using enum MetricType;
        case BatteryVoltage:
        case ChargeLevel:
        case IsCharging:
            return true;
        default:
            return false;
    }
}

bool Axp192::getMetric(MetricType type, MetricData& data) {
    switch (type) {
        using enum MetricType;
        case BatteryVoltage: {
            float voltage;
            if (axp192_read(&axpDevice, AXP192_BATTERY_VOLTAGE, &voltage) == ESP_OK) {
                data.valueAsUint32 = (uint32_t)std::max((voltage * 1000.f), 0.0f);
                return true;
            } else {
                return false;
            }
        }
        case ChargeLevel: {
            float vbat, charge_current;
            if (
                axp192_read(&axpDevice, AXP192_BATTERY_VOLTAGE, &vbat) == ESP_OK &&
                axp192_read(&axpDevice, AXP192_CHARGE_CURRENT, &charge_current) == ESP_OK
            ) {
                float max_voltage = 4.20f;
                float min_voltage = 2.69f; // From M5Unified
                float voltage_correction = (charge_current > 0.01f) ? -0.1f : 0.f; // Roughly 0.1V drop when ccharging
                float corrected_voltage = vbat + voltage_correction;
                if (corrected_voltage > 2.69f) {
                    float charge_factor = (corrected_voltage - min_voltage) / (max_voltage - min_voltage);
                    data.valueAsUint8 = (uint8_t)(charge_factor * 100.f);
                } else {
                    data.valueAsUint8 = 0;
                }
                return true;
            } else {
                return false;
            }
        }
        case IsCharging: {
            float charge_current;
            if (axp192_read(&axpDevice, AXP192_CHARGE_CURRENT, &charge_current) == ESP_OK) {
                data.valueAsBool = charge_current > 0.001f;
                return true;
            } else {
                return false;
            }
        }
        case Current: {
            float charge_current, discharge_current;
            if (
                axp192_read(&axpDevice, AXP192_CHARGE_CURRENT, &charge_current) == ESP_OK &&
                axp192_read(&axpDevice, AXP192_DISCHARGE_CURRENT, &discharge_current) == ESP_OK
            ) {
                if (charge_current > 0.0f) {
                    data.valueAsInt32 = (int32_t) (charge_current * 1000.0f);
                } else {
                    data.valueAsInt32 = -(int32_t) (discharge_current * 1000.0f);
                }
                return true;
            } else {
                return false;
            }
        }
        default:
            return false;
    }
}

bool Axp192::isAllowedToCharge() const {
    uint8_t buffer;
    if (axp192_read(&axpDevice, AXP192_CHARGE_CONTROL_1, &buffer) == ESP_OK) {
        return buffer & 0x80;
    } else {
        return false;
    }
}

void Axp192::setAllowedToCharge(bool canCharge) {
    uint8_t buffer;
    if (axp192_read(&axpDevice, AXP192_CHARGE_CONTROL_1, &buffer) == ESP_OK) {
        buffer = (buffer & 0x7F) + (canCharge ? 0x80 : 0x00);
        axp192_write(&axpDevice, AXP192_CHARGE_CONTROL_1, buffer);
    }
}
