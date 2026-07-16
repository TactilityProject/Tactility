#include "devices/Display.h"

#include <PwmBacklight.h>
#include <Tactility/hal/Configuration.h>

using namespace tt::hal;

static bool initBoot() {
    return driver::pwmbacklight::init(LCD_PIN_BACKLIGHT);
}

static DeviceVector createDevices() {
    return {
        createDisplay(),
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
