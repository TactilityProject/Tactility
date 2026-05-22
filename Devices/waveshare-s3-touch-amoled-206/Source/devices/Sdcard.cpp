#include "Sdcard.h"

#include <tactility/device.h>

#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/hal/sdcard/SpiSdCardDevice.h>

using tt::hal::sdcard::SpiSdCardDevice;

constexpr auto SDCARD_PIN_CS = GPIO_NUM_17;
constexpr auto SDCARD_PIN_MISO = GPIO_NUM_3;
constexpr auto SDCARD_PIN_MOSI = GPIO_NUM_1;
constexpr auto SDCARD_PIN_SCLK = GPIO_NUM_2;

std::shared_ptr<SdCardDevice> createSdCard() {
    auto configuration = std::make_unique<SpiSdCardDevice::Config>(
        SDCARD_PIN_CS,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        SdCardDevice::MountBehaviour::AtBoot,
        tt::lvgl::getSyncLock(),
        std::vector<gpio_num_t> { },
        SPI3_HOST
    );

    // Find the spi1 device from device tree
    ::Device* spiController = device_find_by_name("spi1");
    auto sdcard = std::make_shared<SpiSdCardDevice>(
        std::move(configuration),
        spiController
    );
    return std::static_pointer_cast<SdCardDevice>(sdcard);
}
