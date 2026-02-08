#include "InitBoot.h"
#include "devices/Display.h"
#include "devices/SdCard.h"
#include <driver/gpio.h>

#include <Tactility/hal/Configuration.h>
#include <Axp2101Power.h>

using namespace tt::hal;

static DeviceVector createDevices() {
    return {
        axp2101,
        aw9523,
        std::make_shared<Axp2101Power>(axp2101),
        createSdCard(),
        createDisplay()
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
