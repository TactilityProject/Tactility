#include "SdCard.h"

#include <tactility/device.h>
#include <Tactility/hal/sdcard/SpiSdCardDevice.h>

using tt::hal::sdcard::SpiSdCardDevice;

constexpr auto CROWPANEL_SDCARD_PIN_CS = GPIO_NUM_7;

std::shared_ptr<SdCardDevice> createSdCard() {
    auto configuration = std::make_unique<SpiSdCardDevice::Config>(
        CROWPANEL_SDCARD_PIN_CS,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        SdCardDevice::MountBehaviour::AtBoot,
        nullptr,
        std::vector<gpio_num_t>(),
        SPI3_HOST
    );

    auto* spi_controller = device_find_by_name("spi1");
    check(spi_controller, "spi1 not found");

    return std::make_shared<SpiSdCardDevice>(
        std::move(configuration),
        spi_controller
    );
}
