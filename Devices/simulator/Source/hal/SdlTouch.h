#pragma once

#include "Tactility/hal/touch/TouchDevice.h"
#include <Tactility/Check.h>
#include <Tactility/TactilityCore.h>

class SdlTouch final : public tt::hal::touch::TouchDevice {

    lv_indev_t* _Nullable handle = nullptr;

public:

    std::string getName() const override { return "SDL Mouse"; }

    std::string getDescription() const override { return "SDL mouse/touch pointer device"; }

    bool start() override { return true; }

    bool stop() override { check(false, "Not supported"); }

    bool supportsLvgl() const override { return true; }

    bool startLvgl(lv_display_t* display) override {
        handle = lv_sdl_mouse_create();
        return handle != nullptr;
    }

    bool stopLvgl() override { check(false, "Not supported"); }

    lv_indev_t* _Nullable getLvglIndev() override { return handle; }

    bool supportsTouchDriver() override { return false; }

    std::shared_ptr<tt::hal::touch::TouchDriver> _Nullable getTouchDriver() override { return nullptr; };
};

