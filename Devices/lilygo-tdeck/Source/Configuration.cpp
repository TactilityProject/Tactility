#include "devices/Power.h"
#include "devices/TrackballDevice.h"

#include <Tactility/hal/Configuration.h>

bool initBoot();

using namespace tt::hal;

static std::vector<std::shared_ptr<tt::hal::Device>> createDevices() {
    return {
        createPower()
        // std::make_shared<TrackballDevice>(),
    };
}

extern const Configuration hardwareConfiguration = {
    .createDevices = createDevices
};
