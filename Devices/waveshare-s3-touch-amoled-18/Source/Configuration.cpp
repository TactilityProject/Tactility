#include "devices/Display.h"
#include "devices/Sdcard.h"

#include <Tactility/hal/Configuration.h>

using namespace tt::hal;

static DeviceVector createDevices() {
    auto devices = DeviceVector();

    auto display = createDisplay();
    if (display) {
        devices.push_back(display);
    }

    auto sdcard = createSdCard();
    if (sdcard) {
        devices.push_back(sdcard);
    }

    return devices;
}

extern const Configuration hardwareConfiguration = {
    .initBoot = nullptr,
    .createDevices = createDevices
};