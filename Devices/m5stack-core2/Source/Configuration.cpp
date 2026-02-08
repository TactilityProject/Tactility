#include "devices/Display.h"
#include "devices/SdCard.h"
#include "devices/Power.h"

#include <Tactility/hal/Configuration.h>

using namespace tt::hal;

static bool initBoot() {
    return initAxp();
}

static DeviceVector createDevices() {
    return {
        getAxp192(),
        createSdCard(),
        createDisplay()
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
