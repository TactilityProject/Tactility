#include "PwmBacklight.h"
#include "devices/Display.h"

#include <Tactility/hal/Configuration.h>

using namespace tt::hal;

static bool initBoot() {
    driver::pwmbacklight::init(GPIO_NUM_27);
    // Delay is required, or GUI will lock up.
    // It's possibly related to the increased power draw when turning on the backlight.
    vTaskDelay(100);
    return true;
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
