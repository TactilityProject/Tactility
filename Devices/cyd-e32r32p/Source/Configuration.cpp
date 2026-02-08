#include "devices/SdCard.h"
#include "devices/Display.h"
#include "devices/Power.h"
#include <driver/gpio.h>

#include <Tactility/hal/Configuration.h>
#include <Tactility/lvgl/LvglSync.h>
#include <PwmBacklight.h>

using namespace tt::hal;

static bool initBoot() {
    return driver::pwmbacklight::init(DISPLAY_BACKLIGHT_PIN);
}

static tt::hal::DeviceVector createDevices() {
    return {
        createPower(),
        createDisplay(),
        createSdCard()
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};