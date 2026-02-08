#include "SdCard.h"

#include <tactility/device.h>
#include <Tactility/hal/sdcard/SpiSdCardDevice.h>

constexpr auto SDCARD_SPI_HOST = SPI3_HOST;
constexpr auto SDCARD_PIN_CS = GPIO_NUM_10;

using tt::hal::sdcard::SpiSdCardDevice;

std::shared_ptr<SdCardDevice> createSdCard() {
    auto configuration = std::make_unique<SpiSdCardDevice::Config>(
        SDCARD_PIN_CS,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        SdCardDevice::MountBehaviour::AtBoot,
        nullptr,
        std::vector<gpio_num_t>(),
        SDCARD_SPI_HOST
    );

    auto* spi_controller = device_find_by_name("spi1");
    check(spi_controller, "spi1 not found");

    return std::make_shared<SpiSdCardDevice>(
        std::move(configuration),
        spi_controller
    );
}
