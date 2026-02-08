#include "devices/Display.h"
#include "devices/SdCard.h"

#include <Tactility/hal/Configuration.h>

using namespace tt::hal;

static DeviceVector createDevices() {
    auto touch = createTouch();
    return {
        createSdCard(),
        createDisplay(touch)
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = nullptr,
    .createDevices = createDevices
};
