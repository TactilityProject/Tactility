#include "SdCard.h"

#include <tactility/device.h>
#include <Tactility/hal/sdcard/SpiSdCardDevice.h>
#include <Tactility/RecursiveMutex.h>

using tt::hal::sdcard::SpiSdCardDevice;
using SdCardDevice = tt::hal::sdcard::SdCardDevice;

std::shared_ptr<SdCardDevice> createSdCard() {
    auto configuration = std::make_unique<SpiSdCardDevice::Config>(
        SD_CS_PIN,                         // CS pin (IO5 on the module)
        GPIO_NUM_NC,                           // MOSI override: leave NC to use SPI host pins
        GPIO_NUM_NC,                           // MISO override: leave NC
        GPIO_NUM_NC,                           // SCLK override: leave NC
        SdCardDevice::MountBehaviour::AtBoot,
        std::make_shared<tt::RecursiveMutex>(),
        std::vector<gpio_num_t>(),
        SD_SPI_HOST                        // SPI host for SD card (SPI3_HOST)
    );

    auto* spi_controller = device_find_by_name("spi1");
    check(spi_controller, "spi1 not found");

    return std::make_shared<SpiSdCardDevice>(
        std::move(configuration),
        spi_controller
    );
}