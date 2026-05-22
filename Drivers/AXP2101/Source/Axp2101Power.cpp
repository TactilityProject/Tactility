#include "Axp2101Power.h"

bool Axp2101Power::supportsMetric(MetricType type) const {
    switch (type) {
        using enum MetricType;
        case BatteryVoltage:
        case IsCharging:
        case ChargeLevel:
            return true;
        default:
            return false;
    }

    return false;
}

bool Axp2101Power::getMetric(MetricType type, MetricData& data) {
    switch (type) {
        using enum MetricType;
        case BatteryVoltage: {
            float milliVolt;
            if (axpDevice->getBatteryVoltage(milliVolt)) {
                data.valueAsUint32 = (uint32_t)milliVolt;
                return true;
            } else {
                return false;
            }
        }
        case ChargeLevel: {
            uint8_t percentage;
            if (axpDevice->getBatteryPercentage(percentage)) {
                // Sanity-check an uncalibrated fuel gauge: it sometimes
                // sticks at 100 % even when the cell is not fully charged.
                // If the gauge claims 100 % but voltage is < 4.15 V,
                // fall back to the voltage-based estimate.
                if (percentage == 100) {
                    float vbatMillis = 0.0f;
                    if (axpDevice->getBatteryVoltage(vbatMillis) && vbatMillis < 4150.0f) {
                        // fall through to voltage calculation below
                    } else {
                        data.valueAsUint8 = percentage;
                        return true;
                    }
                } else {
                    data.valueAsUint8 = percentage;
                    return true;
                }
            }

            float vbatMillis;
            if (axpDevice->getBatteryVoltage(vbatMillis)) {
                float vbat = vbatMillis / 1000.f;
                float max_voltage = 4.20f;
                float min_voltage = 3.0f;
                if (vbat > min_voltage) {
                    float charge_factor = (vbat - min_voltage) / (max_voltage - min_voltage);
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
            Axp2101::ChargeStatus status;
            if (axpDevice->getChargeStatus(status)) {
                data.valueAsBool = (status == Axp2101::CHARGE_STATUS_CHARGING);
                return true;
            } else {
                return false;
            }
        }
        default:
            return false;
    }
}

bool Axp2101Power::isAllowedToCharge() const {
    bool enabled;
    if (axpDevice->isChargingEnabled(enabled)) {
        return enabled;
    } else {
        return false;
    }
}

void Axp2101Power::setAllowedToCharge(bool canCharge) {
    axpDevice->setChargingEnabled(canCharge);
}
