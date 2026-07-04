#include "ChargeFromVoltage.h"

#include <tactility/log.h>
#include <algorithm>

constexpr auto* TAG = "ChargeFromVoltage";

uint8_t ChargeFromVoltage::estimateCharge(uint32_t milliVolt) const {
    const float volts = std::min((float)milliVolt / 1000.f, batteryVoltageMax);
    if (volts < batteryVoltageMin) {
        return 0;
    }
    const float voltage_percentage = (volts - batteryVoltageMin) / (batteryVoltageMax - batteryVoltageMin);
    const float voltage_factor = std::min(1.0f, voltage_percentage);
    const auto charge_level = (uint8_t) (voltage_factor * 100.f);
    LOG_D(TAG, "mV = %u, scaled = %f, factor = %.2f, result = %d", (unsigned)milliVolt, volts, voltage_factor, charge_level);
    return charge_level;
}
