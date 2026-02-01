#pragma once

#include <tactility/hal/Device.h>

#include <lvgl.h>

namespace tt::hal::keyboard {

class Display;

class KeyboardDevice : public Device {

public:

    Type getType() const override { return Type::Keyboard; }

    virtual bool startLvgl(lv_display_t* display) = 0;
    virtual bool stopLvgl() = 0;

    /** @return true when the keyboard currently is physically attached */
    virtual bool isAttached() const = 0;

    /** Could return nullptr if not started */
    virtual lv_indev_t* getLvglIndev() = 0;
};

}
