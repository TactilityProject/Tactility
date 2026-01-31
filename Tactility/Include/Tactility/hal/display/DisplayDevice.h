#pragma once

#include <tactility/hal/Device.h>

#include <lvgl.h>

namespace tt::hal::touch {
class TouchDevice;
}

namespace tt::hal::display {

class DisplayDriver;

class DisplayDevice : public Device {

public:

    Type getType() const override { return Type::Display; }

    /** Starts the internal driver */
    virtual bool start() = 0;
    virtual bool stop() = 0;

    virtual void setPowerOn(bool turnOn) {}
    virtual bool isPoweredOn() const { return true; }
    virtual bool supportsPowerControl() const { return false; }

    virtual std::shared_ptr<touch::TouchDevice> _Nullable getTouchDevice() = 0;

    /** Set a value in the range [0, 255] */
    virtual void setBacklightDuty(uint8_t backlightDuty) { /* NO-OP */ }
    virtual bool supportsBacklightDuty() const { return false; }

    /** Set a value in the range [0, 255] */
    virtual void setGammaCurve(uint8_t index) { /* NO-OP */ }
    virtual uint8_t getGammaCurveCount() const { return 0; }

    virtual bool supportsLvgl() const = 0;
    virtual bool startLvgl() = 0;
    virtual bool stopLvgl() = 0;

    virtual lv_display_t* _Nullable getLvglDisplay() const = 0;

    virtual bool supportsDisplayDriver() const = 0;
    virtual std::shared_ptr<DisplayDriver> _Nullable getDisplayDriver() = 0;
};

} // namespace tt::hal::display
