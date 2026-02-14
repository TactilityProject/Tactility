#include "devices/Display.h"

#include <PwmBacklight.h>
#include <Tactility/hal/Configuration.h>
#include <ButtonControl.h>

using namespace tt::hal;

static bool initBoot() {
    return driver::pwmbacklight::init(LCD_PIN_BACKLIGHT);
}

static std::vector<std::shared_ptr<tt::hal::Device>> createDevices() {
    return {
        createDisplay(),
        ButtonControl::createTwoButtonControl(35, 0)
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .uiDensity = UiDensity::Compact,
    .createDevices = createDevices
};
