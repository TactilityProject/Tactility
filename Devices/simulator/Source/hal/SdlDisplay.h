#pragma once

#include "SdlTouch.h"
#include <tactility/check.h>
#include <Tactility/hal/display/DisplayDevice.h>

class SdlDisplay final : public tt::hal::display::DisplayDevice {

    lv_disp_t* displayHandle;

public:

    std::string getName() const override { return "SDL Display"; }
    std::string getDescription() const override { return ""; }

    bool start() override { return true; }

    bool stop() override { return true; }

    bool supportsLvgl() const override { return true; }

    bool startLvgl() override {
        displayHandle = lv_sdl_window_create(320, 240);
        lv_sdl_window_set_title(displayHandle, "Tactility");
        return displayHandle != nullptr;
    }

    bool stopLvgl() override {
        lv_display_delete(displayHandle);
        displayHandle = nullptr;
        return true;
    }

    lv_display_t* getLvglDisplay() const override { return displayHandle; }

    std::shared_ptr<tt::hal::touch::TouchDevice> getTouchDevice() override { return std::make_shared<SdlTouch>(); }

    bool supportsDisplayDriver() const override { return false; }
    std::shared_ptr<tt::hal::display::DisplayDriver> getDisplayDriver() override { return nullptr; }
};
