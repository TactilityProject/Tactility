#include "SdCard.hpp"

#include <Tactility/hal/sdcard/SpiSdCardDevice.h>

constexpr auto PAPERS3_SDCARD_PIN_CS = GPIO_NUM_47;

using tt::hal::sdcard::SpiSdCardDevice;

std::shared_ptr<SdCardDevice> createSdCard() {
    auto configuration = std::make_unique<SpiSdCardDevice::Config>(
        PAPERS3_SDCARD_PIN_CS,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        SdCardDevice::MountBehaviour::AtBoot
    );

    return std::make_shared<SpiSdCardDevice>(
        std::move(configuration)
    );
}
