#include "devices/Display.h"
#include "devices/Power.h"
#include "devices/SdCard.h"

#include <Tactility/hal/Configuration.h>

using namespace tt::hal;

static DeviceVector createDevices() {
    return {
        createDisplay(),
        createSdCard(),
        createPower()
    };
}

extern const Configuration hardwareConfiguration = {
    .createDevices = createDevices,
};
