#include "hal/SdlDisplay.h"
#include "hal/SdlKeyboard.h"
#include "hal/SimulatorPower.h"
#include "hal/SimulatorSdCard.h"

#include <src/lv_init.h> // LVGL
#include <Tactility/hal/Configuration.h>

#define TAG "hardware"

using namespace tt::hal;

static std::vector<std::shared_ptr<tt::hal::Device>> createDevices() {
    return {
        std::make_shared<SdlDisplay>(),
        std::make_shared<SdlKeyboard>(),
        std::make_shared<SimulatorPower>(),
        std::make_shared<SimulatorSdCard>()
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = nullptr,
    .createDevices = createDevices,
    .uart = {
        uart::Configuration {
            .name = "/dev/ttyUSB0",
            .baudRate = 115200
        },
        uart::Configuration {
            .name = "/dev/ttyACM0",
            .baudRate = 115200
        }
    }
};
