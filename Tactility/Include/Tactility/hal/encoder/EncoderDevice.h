#pragma once

#include <tactility/hal/Device.h>

#include <lvgl.h>

namespace tt::hal::encoder {

class Display;

class EncoderDevice : public Device {

public:

    Type getType() const override { return Type::Encoder; }

    virtual bool startLvgl(lv_display_t* display) = 0;
    virtual bool stopLvgl() = 0;

    virtual lv_indev_t* _Nullable getLvglIndev() = 0;
};

}
