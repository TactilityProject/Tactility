#include "devices/Display.h"
#include "devices/KeyboardBacklight.h"
#include "devices/Power.h"
#include "devices/Sdcard.h"
#include "devices/TdeckKeyboard.h"
#include "devices/TrackballDevice.h"

#include <Tactility/hal/Configuration.h>
#include <Tactility/lvgl/LvglSync.h>

bool initBoot();

using namespace tt::hal;

static std::vector<std::shared_ptr<tt::hal::Device>> createDevices() {
    return {
        createPower(),
        createDisplay(),
        std::make_shared<TdeckKeyboard>(),
        std::make_shared<KeyboardBacklightDevice>(),
        std::make_shared<TrackballDevice>(),
        createSdCard()
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
