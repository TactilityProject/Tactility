#pragma once

#include "Tactility/hal/touch/TouchDevice.h"
#include <Tactility/TactilityCore.h>
#include <tactility/check.h>

class SdlTouch final : public tt::hal::touch::TouchDevice {

    lv_indev_t* handle = nullptr;

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

    lv_indev_t* getLvglIndev() override { return handle; }

    bool supportsTouchDriver() override { return false; }

    std::shared_ptr<tt::hal::touch::TouchDriver> getTouchDriver() override { return nullptr; };
};

