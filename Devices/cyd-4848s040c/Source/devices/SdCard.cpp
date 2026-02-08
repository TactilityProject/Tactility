#include "SdCard.h"

#include <tactility/device.h>
#include <Tactility/hal/sdcard/SpiSdCardDevice.h>
#include <Tactility/lvgl/LvglSync.h>

using tt::hal::sdcard::SpiSdCardDevice;

std::shared_ptr<SdCardDevice> createSdCard() {
    auto config = std::make_unique<SpiSdCardDevice::Config>(
        GPIO_NUM_42,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        SdCardDevice::MountBehaviour::AtBoot,
        tt::lvgl::getSyncLock(),
        std::vector { GPIO_NUM_39 }
    );

    auto* spi_controller = device_find_by_name("spi0");
    check(spi_controller, "spi0 not found");

    return std::make_shared<SpiSdCardDevice>(
        std::move(config),
        spi_controller
    );
}

