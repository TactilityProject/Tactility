#include "Sdcard.h"

#include <tactility/hal/sdcard/SpiSdCardDevice.h>

#include <tactility/device.h>
#include <Tactility/lvgl/LvglSync.h>

using tt::hal::sdcard::SpiSdCardDevice;

constexpr auto SDCARD_PIN_CS = GPIO_NUM_34;

std::shared_ptr<tt::hal::sdcard::SpiSdCardDevice> createSdCard() {
    auto configuration = std::make_unique<SpiSdCardDevice::Config>(
        SDCARD_PIN_CS,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        tt::hal::sdcard::SdCardDevice::MountBehaviour::Anytime,
        tt::lvgl::getSyncLock(),
        std::vector<gpio_num_t> {}
    );

    ::Device* spiController = device_find_by_name("spi1");
    if (spiController == nullptr) {
        return nullptr;
    }
    return std::make_shared<SpiSdCardDevice>(
        std::move(configuration),
        spiController
    );
}
