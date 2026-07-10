#pragma once

#include "Tactility/hal/power/PowerDevice.h"

#include <Bq27220.h>
#include <tactility/device.h>

#include <memory>

using tt::hal::power::PowerDevice;

// Power device for the LilyGO T-Deck Max. Battery metrics come from the BQ27220
// fuel gauge (I2C 0x55); charging status, charge control and power-off (ship
// mode, REG09 BATFET_DIS) use the SY6970 charger. Newer T-Deck Max revisions
// ship the SY6970 (0x6A) instead of the older BQ25896 (0x6B); this board has
// the SY6970 (confirmed by I2C scan). The two are register-compatible for
// everything used here.
class TdeckmaxPower final : public PowerDevice {
    std::shared_ptr<Bq27220> gauge;
    ::Device* i2c; // i2c0 controller, used to reach the SY6970 charger

    bool configureCharger() const;

public:

    TdeckmaxPower(const std::shared_ptr<Bq27220>& gauge, ::Device* i2c) : gauge(gauge), i2c(i2c) {
        configureCharger();
    }

    std::string getName() const final { return "T-Deck Max Power"; }
    std::string getDescription() const final { return "Battery metrics (BQ27220), SY6970 charging and power-off"; }

    bool supportsMetric(MetricType type) const override;
    bool getMetric(MetricType type, MetricData& data) override;

    bool supportsChargeControl() const override { return true; }
    bool isAllowedToCharge() const override;
    void setAllowedToCharge(bool canCharge) override;

    bool supportsPowerOff() const override { return true; }
    void powerOff() override;
};
