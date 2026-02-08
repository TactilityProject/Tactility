#include "devices/Display.h"
#include "devices/SdCard.h"
#include "devices/TpagerEncoder.h"
#include "devices/TpagerKeyboard.h"
#include "devices/TpagerPower.h"
#include <driver/gpio.h>

#include <Tactility/hal/Configuration.h>
#include <Bq25896.h>
#include <Drv2605.h>

bool tpagerInit();

using namespace tt::hal;

static DeviceVector createDevices() {
    auto bq27220 = std::make_shared<Bq27220>(I2C_NUM_0);
    auto power = std::make_shared<TpagerPower>(bq27220);

    auto tca8418 = std::make_shared<Tca8418>(I2C_NUM_0);
    auto keyboard = std::make_shared<TpagerKeyboard>(tca8418);

    return std::vector<std::shared_ptr<tt::hal::Device>> {
        tca8418,
        std::make_shared<Bq25896>(I2C_NUM_0),
        bq27220,
        std::make_shared<Drv2605>(I2C_NUM_0),
        power,
        createTpagerSdCard(),
        createDisplay(),
        keyboard,
        std::make_shared<TpagerEncoder>()
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = tpagerInit,
    .createDevices = createDevices
};
