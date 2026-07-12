#include "devices/CardputerKeyboard.h"
#include "devices/CardputerPower.h"

#include <tactility/device.h>
#include <Tactility/hal/Configuration.h>

#include <Tca8418.h>

using namespace tt::hal;

static DeviceVector createDevices() {
    auto tca8418 = std::make_shared<Tca8418>(device_find_by_name("i2c_internal"));
    return {
        tca8418,
        std::make_shared<CardputerKeyboard>(tca8418),
        std::make_shared<CardputerPower>()
    };
}

extern const Configuration hardwareConfiguration = {
    .createDevices = createDevices
};
