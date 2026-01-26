#include "SdCard.h"

#include <Tactility/hal/sdcard/SpiSdCardDevice.h>

constexpr auto SDCARD_PIN_CS = GPIO_NUM_42;

using tt::hal::sdcard::SpiSdCardDevice;

std::shared_ptr<SdCardDevice> createSdCard() {
    auto configuration = std::make_unique<SpiSdCardDevice::Config>(
        SDCARD_PIN_CS,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        SdCardDevice::MountBehaviour::AtBoot,
        tt::hal::spi::getLock(SPI3_HOST),
        std::vector<gpio_num_t>(),
        SPI3_HOST
    );

    return std::make_shared<SpiSdCardDevice>(
        std::move(configuration)
    );
}
