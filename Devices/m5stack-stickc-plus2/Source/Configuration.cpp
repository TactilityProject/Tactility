#include "devices/Display.h"
#include <driver/gpio.h>

#include <Tactility/hal/Configuration.h>
#include <ButtonControl.h>
#include <PwmBacklight.h>

using namespace tt::hal;

bool initBoot() {
    // CH552 applies 4 V to GPIO 0, which reduces Wi-Fi sensitivity
    // Setting output to high adds a bias of 3.3 V and suppresses over-voltage:
    gpio::configure(0, gpio::Mode::Output, false, false);
    gpio::setLevel(0, true);

    // "Hold power" pin: must be set to high to keep the device powered on:
    gpio::configure(4, gpio::Mode::Output, false, false);
    gpio::setLevel(4, true);
    return driver::pwmbacklight::init(GPIO_NUM_27, 512);
}

static DeviceVector createDevices() {
    return {
        ButtonControl::createTwoButtonControl(37, 39),
        createDisplay()
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .uiDensity = UiDensity::Compact,
    .createDevices = createDevices
};
