#include "devices/Display.h"
#include "devices/SdCard.h"
#include "devices/Power.h"

#include <Tactility/hal/Configuration.h>

using namespace tt::hal;

bool initBoot();

static DeviceVector createDevices() {
    return {
        createPower(),
        createDisplay(),
        createSdCard(),
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
