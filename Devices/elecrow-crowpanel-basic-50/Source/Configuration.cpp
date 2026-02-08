#include "devices/Display.h"
#include "devices/SdCard.h"
#include <driver/gpio.h>

#include <Tactility/hal/Configuration.h>
#include <PwmBacklight.h>

using namespace tt::hal;

static bool initBoot() {
    // Note: I tried 100 Hz to 100 kHz and couldn't get the flickering to stop
    return driver::pwmbacklight::init(GPIO_NUM_2);
}

static DeviceVector createDevices() {
    return {
        createDisplay(),
        createSdCard(),
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
