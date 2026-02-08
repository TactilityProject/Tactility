#include "SdCard.h"

#include <tactility/device.h>
#include <Tactility/hal/sdcard/SpiSdCardDevice.h>

using tt::hal::sdcard::SpiSdCardDevice;

std::shared_ptr<SdCardDevice> createSdCard() {
    auto configuration = std::make_unique<SpiSdCardDevice::Config>(
        // See https://github.com/Elecrow-RD/CrowPanel-Advance-HMI-ESP32-AI-Display/blob/master/5.0/factory_code/factory_code.ino
        GPIO_NUM_0, // It's actually not connected, but in the demo pin 0 is used
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        SdCardDevice::MountBehaviour::AtBoot
    );

    auto* spi_controller = device_find_by_name("spi0");
    check(spi_controller, "spi0 not found");

    return std::make_shared<SpiSdCardDevice>(
        std::move(configuration),
        spi_controller
    );
}
