#include "SdCard.h"

#include <tactility/device.h>
#include <Tactility/hal/sdcard/SpiSdCardDevice.h>
#include <Tactility/lvgl/LvglSync.h>

using tt::hal::sdcard::SpiSdCardDevice;

constexpr auto TPAGER_SDCARD_PIN_CS = GPIO_NUM_21;
constexpr auto TPAGER_LCD_PIN_CS = GPIO_NUM_38;
constexpr auto TPAGER_RADIO_PIN_CS = GPIO_NUM_36;

std::shared_ptr<SdCardDevice> createTpagerSdCard() {
    auto configuration = std::make_unique<SpiSdCardDevice::Config>(
        TPAGER_SDCARD_PIN_CS,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        SdCardDevice::MountBehaviour::AtBoot,
        tt::lvgl::getSyncLock(),
        std::vector {
            TPAGER_RADIO_PIN_CS,
            TPAGER_LCD_PIN_CS
        }
    );

    auto* spi_controller = device_find_by_name("spi0");
    check(spi_controller, "spi0 not found");

    return std::make_shared<SpiSdCardDevice>(
        std::move(configuration),
        spi_controller
    );
}
