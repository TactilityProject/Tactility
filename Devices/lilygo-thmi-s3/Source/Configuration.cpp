#include "devices/Power.h"
#include "devices/SdCard.h"
#include "devices/Display.h"

#include <ButtonControl.h>
#include <Tactility/hal/Configuration.h>

bool initBoot();

using namespace tt::hal;

static std::vector<std::shared_ptr<tt::hal::Device>> createDevices() {
    return {
        createPower(),
        createSdCard(),
        createDisplay(),
        ButtonControl::createTwoButtonControl(0, 14),
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
