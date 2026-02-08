#include "SdCard.h"

#include <tactility/device.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/hal/sdcard/SpiSdCardDevice.h>

#define UNPHONE_SDCARD_PIN_CS GPIO_NUM_43
#define UNPHONE_LCD_PIN_CS GPIO_NUM_48
#define UNPHONE_LORA_PIN_CS GPIO_NUM_44
#define UNPHONE_TOUCH_PIN_CS GPIO_NUM_38

using tt::hal::sdcard::SpiSdCardDevice;

std::shared_ptr<SdCardDevice> createSdCard() {
    auto configuration = std::make_unique<SpiSdCardDevice::Config>(
        UNPHONE_SDCARD_PIN_CS,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        SdCardDevice::MountBehaviour::AtBoot,
        tt::lvgl::getSyncLock(),
        std::vector {
            UNPHONE_LORA_PIN_CS,
            UNPHONE_LCD_PIN_CS,
            UNPHONE_TOUCH_PIN_CS
        }
    );

    auto* spi_controller = device_find_by_name("spi0");
    check(spi_controller, "spi0 not found");

    return std::make_shared<SpiSdCardDevice>(
        std::move(configuration),
        spi_controller
    );
}
