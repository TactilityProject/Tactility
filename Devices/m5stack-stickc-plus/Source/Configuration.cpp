#include "devices/Display.h"
#include "devices/Power.h"

#include <Tactility/hal/Configuration.h>
#include <ButtonControl.h>

using namespace tt::hal;

static bool initBoot() {
    // CH552 applies 4 V to GPIO 0, which reduces Wi-Fi sensitivity
    // Setting output to high adds a bias of 3.3 V and suppresses over-voltage:
    gpio::configure(0, gpio::Mode::Output, false, false);
    gpio::setLevel(0, true);

    return initAxp();
}

static DeviceVector createDevices() {
    return {
        getAxp192(),
        ButtonControl::createTwoButtonControl(37, 39),
        createDisplay()
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
