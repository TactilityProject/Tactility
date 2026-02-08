#include "PwmBacklight.h"
#include "devices/Display.h"
#include "devices/SdCard.h"
#include <driver/gpio.h>

#include <Tactility/hal/Configuration.h>

using namespace tt::hal;

static bool initBoot() {
    // Display backlight
    return driver::pwmbacklight::init(GPIO_NUM_2, 200);
}

static DeviceVector createDevices() {
    return {
        createDisplay(),
        createSdCard()
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
