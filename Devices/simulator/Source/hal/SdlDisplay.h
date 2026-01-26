#pragma once

#include "SdlTouch.h"
#include <Tactility/Check.h>
#include <Tactility/hal/display/DisplayDevice.h>

/** Hack: variable comes from LvglTask.cpp */
extern lv_disp_t* displayHandle;

class SdlDisplay final : public tt::hal::display::DisplayDevice {

public:

    std::string getName() const override { return "SDL Display"; }
    std::string getDescription() const override { return ""; }

    bool start() override { return true; }
    bool stop() override { check(false, "Not supported"); }

    bool supportsLvgl() const override { return true; }
    bool startLvgl() override { return displayHandle != nullptr; }
    bool stopLvgl() override { check(false, "Not supported"); }
    lv_display_t* _Nullable getLvglDisplay() const override { return displayHandle; }

    std::shared_ptr<tt::hal::touch::TouchDevice> _Nullable getTouchDevice() override { return std::make_shared<SdlTouch>(); }

    bool supportsDisplayDriver() const override { return false; }
    std::shared_ptr<tt::hal::display::DisplayDriver> _Nullable getDisplayDriver() override { return nullptr; }
};
