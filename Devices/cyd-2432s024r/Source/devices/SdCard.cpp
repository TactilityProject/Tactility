#include "SdCard.h"
#include <tactility/device.h>
#include <Tactility/hal/sdcard/SpiSdCardDevice.h>

using tt::hal::sdcard::SpiSdCardDevice;

std::shared_ptr<SdCardDevice> createSdCard() {
    auto config = std::make_unique<SpiSdCardDevice::Config>(
        GPIO_NUM_5,   // CS
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        SdCardDevice::MountBehaviour::AtBoot,
        nullptr,
        std::vector<gpio_num_t>(),
        SPI2_HOST     // shared bus
    );

    auto* spi_controller = device_find_by_name("spi0");
    check(spi_controller, "spi0 not found");

    return std::make_shared<SpiSdCardDevice>(std::move(config), spi_controller);
}
