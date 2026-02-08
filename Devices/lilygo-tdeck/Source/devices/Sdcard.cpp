#include "Sdcard.h"

#include <tactility/device.h>

#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/hal/sdcard/SpiSdCardDevice.h>

using tt::hal::sdcard::SpiSdCardDevice;

constexpr auto TDECK_SDCARD_PIN_CS = GPIO_NUM_39;
constexpr auto TDECK_LCD_PIN_CS = GPIO_NUM_12;
constexpr auto TDECK_RADIO_PIN_CS = GPIO_NUM_9;

std::shared_ptr<SdCardDevice> createSdCard() {
    auto configuration = std::make_unique<SpiSdCardDevice::Config>(
        TDECK_SDCARD_PIN_CS,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        SdCardDevice::MountBehaviour::AtBoot,
        tt::lvgl::getSyncLock(),
        std::vector {
            TDECK_RADIO_PIN_CS,
            TDECK_LCD_PIN_CS
        }
    );

    ::Device* spiController = device_find_by_name("spi0");
    check(spiController != nullptr, "spi0 not found");
    return std::make_shared<SpiSdCardDevice>(
        std::move(configuration),
        spiController
    );
}
