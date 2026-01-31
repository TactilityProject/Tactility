#include "Xpt2046Power.h"
#include "Xpt2046Touch.h"

#include <Tactility/Logger.h>
#include <tactility/hal/Device.h>

static const auto LOGGER = tt::Logger("Xpt2046Power");

constexpr auto BATTERY_VOLTAGE_MIN = 3.2f;
constexpr auto BATTERY_VOLTAGE_MAX = 4.2f;
constexpr auto MAX_VOLTAGE_SAMPLES = 15;

static std::shared_ptr<Xpt2046Touch> findXp2046TouchDevice() {
    // Make a safe copy
    auto touch = tt::hal::findFirstDevice<tt::hal::touch::TouchDevice>(tt::hal::Device::Type::Touch);
    if (touch == nullptr) {
        LOGGER.error("Touch device not found");
        return nullptr;
    }

    if (touch->getName() != "XPT2046") {
        LOGGER.error("Touch device name mismatch");
        return nullptr;
    }

    return std::reinterpret_pointer_cast<Xpt2046Touch>(touch);
}

static uint8_t estimateChargeLevelFromVoltage(uint32_t milliVolt) {
    float volts = std::min((float)milliVolt / 1000.f, BATTERY_VOLTAGE_MAX);
    float voltage_percentage = (volts - BATTERY_VOLTAGE_MIN) / (BATTERY_VOLTAGE_MAX - BATTERY_VOLTAGE_MIN);
    float voltage_factor = std::min(1.0f, voltage_percentage);
    auto charge_level = (uint8_t) (voltage_factor * 100.f);
    LOGGER.verbose("mV = {}, scaled = {}, factor = {}, result = {}", milliVolt, volts, voltage_factor, charge_level);
    return charge_level;
}

bool Xpt2046Power::supportsMetric(MetricType type) const {
    switch (type) {
        using enum MetricType;
        case BatteryVoltage:
        case ChargeLevel:
            return true;
        default:
            return false;
    }
}

bool Xpt2046Power::getMetric(MetricType type, MetricData& data) {
    switch (type) {
        using enum MetricType;
        case BatteryVoltage:
            return readBatteryVoltageSampled(data.valueAsUint32);
        case ChargeLevel: {
            uint32_t milli_volt;
            if (readBatteryVoltageSampled(milli_volt)) {
                data.valueAsUint8 = estimateChargeLevelFromVoltage(milli_volt);
                return true;
            } else {
                return false;
            }
        }
        default:
            return false;
    }
}

bool Xpt2046Power::readBatteryVoltageOnce(uint32_t& output) {
    if (xptTouch == nullptr) {
        xptTouch = findXp2046TouchDevice();
        if (xptTouch == nullptr) {
            LOGGER.error("XPT2046 touch device not found");
            return false;
        }
    }

    float vbat;
    if (!xptTouch->getVBat(vbat)) {
        return false;
    }

    // Convert to mV
    output = (uint32_t)(vbat * 1000.f);
    return true;
}

bool Xpt2046Power::readBatteryVoltageSampled(uint32_t& output) {
    size_t samples_read = 0;
    uint32_t sample_accumulator = 0;
    uint32_t sample_read_buffer;

    for (size_t i = 0; i < MAX_VOLTAGE_SAMPLES; ++i) {
        if (readBatteryVoltageOnce(sample_read_buffer)) {
            sample_accumulator += sample_read_buffer;
            samples_read++;
        }
    }

    if (samples_read > 0) {
        output = sample_accumulator / samples_read;
        return true;
    } else {
        return false;
    }
}
