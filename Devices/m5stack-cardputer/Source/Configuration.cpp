#include "devices/CardputerEncoder.h"
#include "devices/CardputerKeyboard.h"
#include "devices/CardputerPower.h"

#include <Tactility/hal/Configuration.h>

using namespace tt::hal;

static DeviceVector createDevices() {
    return {
        std::make_shared<CardputerKeyboard>(),
        std::make_shared<CardputerEncoder>(),
        std::make_shared<CardputerPower>()
    };
}

extern const Configuration hardwareConfiguration = {
    .createDevices = createDevices
};
