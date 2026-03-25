#include "devices/Display.h"
#include "devices/Power.h"
#include <driver/gpio.h>

#include <Tactility/hal/Configuration.h>
#include <ButtonControl.h>
#include <PwmBacklight.h>

using namespace tt::hal;

bool initBoot() {
    return driver::pwmbacklight::init(GPIO_NUM_38, 512);
}

static DeviceVector createDevices() {
    return {
        createPower(),
        ButtonControl::createTwoButtonControl(11, 12), // top button, side button
        createDisplay()
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
