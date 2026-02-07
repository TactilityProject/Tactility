#include "devices/SdCard.h"
#include "devices/Display.h"

#include <Tactility/hal/Configuration.h>
#include <Tactility/lvgl/LvglSync.h>
#include <PwmBacklight.h>

static bool initBoot() {
    return driver::pwmbacklight::init(LCD_BACKLIGHT_PIN);
}

static tt::hal::DeviceVector createDevices() {
    return {
        createDisplay(),
        createSdCard()
    };
}

extern const tt::hal::Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
