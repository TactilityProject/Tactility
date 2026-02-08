#include "devices/Display.h"
#include <driver/gpio.h>

#include <Tactility/hal/Configuration.h>
#include <PwmBacklight.h>

using namespace tt::hal;

constexpr auto* TAG = "Waveshare";

static DeviceVector createDevices() {
    return {
        createDisplay()
    };
}

static bool initBoot() {
    return driver::pwmbacklight::init(GPIO_NUM_2, 256);
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .uiScale = UiScale::Smallest,
    .createDevices = createDevices
};
