#include "SdCard.h"

#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/hal/sdcard/SdmmcDevice.h>

using tt::hal::sdcard::SdmmcDevice;

std::shared_ptr<SdCardDevice> createSdCard() {
    auto configuration = std::make_unique<SdmmcDevice::Config>(
        SD_DIO_SCLK, //CLK
        SD_DIO_CMD, //CMD
        SD_DIO_DATA0, //D0
        SD_DIO_NC, //D1
        SD_DIO_NC, //D2
        SD_DIO_NC, //D3
        SdCardDevice::MountBehaviour::AtBoot,
        SD_DIO_BUS_WIDTH
    );

    return std::make_shared<SdmmcDevice>(
        std::move(configuration)
    );
}
