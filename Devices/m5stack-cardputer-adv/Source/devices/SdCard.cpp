#include "SdCard.h"

#include <Tactility/hal/sdcard/SpiSdCardDevice.h>

constexpr auto SDCARD_PIN_CS = GPIO_NUM_12;
constexpr auto EXPANSION_HEADER_PIN_CS = GPIO_NUM_5;

using tt::hal::sdcard::SpiSdCardDevice;

std::shared_ptr<SdCardDevice> createSdCard() {
    auto configuration = std::make_unique<SpiSdCardDevice::Config>(
        SDCARD_PIN_CS,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        SdCardDevice::MountBehaviour::AtBoot,
        tt::hal::spi::getLock(SPI3_HOST),
        std::vector { EXPANSION_HEADER_PIN_CS },
        SPI3_HOST
    );

    return std::make_shared<SpiSdCardDevice>(
        std::move(configuration)
    );
}
