#include "TpagerPower.h"

#include <Bq25896.h>
#include <Tactility/Logger.h>

static const auto LOGGER = tt::Logger("TpagerPower");

TpagerPower::~TpagerPower() {}

bool TpagerPower::supportsMetric(MetricType type) const {
    switch (type) {
        using enum MetricType;
        case IsCharging:
        case Current:
        case BatteryVoltage:
        case ChargeLevel:
            return true;
        default:
            return false;
    }

    return false; // Safety guard for when new enum values are introduced
}

bool TpagerPower::getMetric(MetricType type, MetricData& data) {
    uint16_t u16 = 0;
    int16_t s16 = 0;
    switch (type) {
        using enum MetricType;
        case IsCharging:
            Bq27220::BatteryStatus status;
            if (gauge->getBatteryStatus(status)) {
                data.valueAsBool = !status.reg.DSG;
                return true;
            }
            return false;
        case Current:
            if (gauge->getCurrent(s16)) {
                data.valueAsInt32 = s16;
                return true;
            } else {
                return false;
            }
        case BatteryVoltage:
            if (gauge->getVoltage(u16)) {
                data.valueAsUint32 = u16;
                return true;
            } else {
                return false;
            }
        case ChargeLevel:
            if (gauge->getStateOfCharge(u16)) {
                data.valueAsUint8 = u16;
                return true;
            } else {
                return false;
            }
        default:
            return false;
    }
}

void TpagerPower::powerOff() {
    auto device = tt::hal::findDevice([](auto device) {
        return device->getName() == "BQ25896";
    });

    if (device == nullptr) {
        LOGGER.error("BQ25896 not found");
        return;
    }

    auto bq25896 = std::reinterpret_pointer_cast<Bq25896>(device);
    if (bq25896 != nullptr) {
        bq25896->powerOff();
    }
}
