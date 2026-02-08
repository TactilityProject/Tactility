#include "SdCard.h"

#include <tactility/device.h>
#include <Tactility/hal/sdcard/SpiSdCardDevice.h>
#include <Tactility/lvgl/LvglSync.h>

using tt::hal::sdcard::SpiSdCardDevice;

std::shared_ptr<SdCardDevice> createSdCard() {
    auto configuration = std::make_unique<SpiSdCardDevice::Config>(
        GPIO_NUM_41,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        SdCardDevice::MountBehaviour::AtBoot,
        tt::lvgl::getSyncLock(),
        std::vector<gpio_num_t>(),
        SPI2_HOST
    );

    ::Device* spiController = device_find_by_name("spi0");
    check(spiController != nullptr, "spi0 not found");

    return std::make_shared<SpiSdCardDevice>(
        std::move(configuration),
        spiController
    );
}
