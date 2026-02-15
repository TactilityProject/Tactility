#include "devices/Display.h"
#include "devices/SdCard.h"
#include "devices/CardputerKeyboard.h"
#include "devices/CardputerPower.h"
#include <driver/gpio.h>

#include <Tactility/hal/Configuration.h>

#include <PwmBacklight.h>
#include <Tca8418.h>

using namespace tt::hal;

static bool initBoot() {
    return driver::pwmbacklight::init(LCD_PIN_BACKLIGHT, 512);
}

static DeviceVector createDevices() {
    auto tca8418 = std::make_shared<Tca8418>(I2C_NUM_0);
    return {
        createSdCard(),
        createDisplay(),
        tca8418,
        std::make_shared<CardputerKeyboard>(tca8418),
        std::make_shared<CardputerPower>()
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
