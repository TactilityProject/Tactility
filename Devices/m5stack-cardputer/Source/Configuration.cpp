#include "devices/Display.h"
#include "devices/SdCard.h"
#include "devices/CardputerEncoder.h"
#include "devices/CardputerKeyboard.h"
#include "devices/CardputerPower.h"
#include <driver/gpio.h>

#include <PwmBacklight.h>
#include <Tactility/hal/Configuration.h>

using namespace tt::hal;

static bool initBoot() {
    return driver::pwmbacklight::init(LCD_PIN_BACKLIGHT, 512);
}

static DeviceVector createDevices() {
    return {
        createSdCard(),
        createDisplay(),
        std::make_shared<CardputerKeyboard>(),
        std::make_shared<CardputerEncoder>(),
        std::make_shared<CardputerPower>()
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
