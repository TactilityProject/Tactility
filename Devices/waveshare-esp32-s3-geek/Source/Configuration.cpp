#include "devices/Display.h"
#include "devices/SdCard.h"

#include <Tactility/hal/Configuration.h>
#include <PwmBacklight.h>
#include <ButtonControl.h>

using namespace tt::hal;

static DeviceVector createDevices() {
    return {
        createDisplay(),
        createSdCard(),
        ButtonControl::createOneButtonControl(0)
    };
}

static bool initBoot() {
    return driver::pwmbacklight::init(LCD_PIN_BACKLIGHT);
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .uiDensity = UiDensity::Compact,
    .createDevices = createDevices
};