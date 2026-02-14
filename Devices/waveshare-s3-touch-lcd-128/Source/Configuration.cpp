#include "devices/Display.h"
#include "devices/SdCard.h"
#include <driver/gpio.h>

#include <Tactility/hal/Configuration.h>
#include <PwmBacklight.h>

using namespace tt::hal;

static DeviceVector createDevices() {
    return {
        createDisplay(),
        createSdCard()
    };
}

static bool initBoot() {
    return driver::pwmbacklight::init(GPIO_NUM_2, 256);
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .uiDensity = UiDensity::Compact,
    .createDevices = createDevices
};
