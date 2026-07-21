#include "hal/SdlDisplay.h"
#include "hal/SdlKeyboard.h"
#include "hal/SimulatorPower.h"

#include <Tactility/hal/Configuration.h>

#define TAG "hardware"

using namespace tt::hal;

static std::vector<std::shared_ptr<tt::hal::Device>> createDevices() {
    return {
        std::make_shared<SdlDisplay>(),
        std::make_shared<SdlKeyboard>(),
        std::make_shared<SimulatorPower>(),
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = nullptr,
    .createDevices = createDevices
};
