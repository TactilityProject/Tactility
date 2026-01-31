#pragma once

#include <tactility/hal/Device.h>
#include "TouchDriver.h"

#include <lvgl.h>

namespace tt::hal::touch {

class Display;

class TouchDevice : public Device {

public:

    Type getType() const override { return Type::Touch; }

    virtual bool start() = 0;
    virtual bool stop() = 0;

    virtual bool supportsLvgl() const = 0;
    virtual bool startLvgl(lv_display_t* display) = 0;
    virtual bool stopLvgl() = 0;

    virtual lv_indev_t* _Nullable getLvglIndev() = 0;

    virtual bool supportsTouchDriver() = 0;

    virtual std::shared_ptr<TouchDriver> _Nullable getTouchDriver() = 0;
};

}
