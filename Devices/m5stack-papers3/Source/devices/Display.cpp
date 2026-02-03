#include "Display.h"
#include <Gt911Touch.h>
#include <FastEpdDisplay.h>
#include <Tactility/lvgl/LvglSync.h>

std::shared_ptr<tt::hal::touch::TouchDevice> createTouch() {
    auto configuration = std::make_unique<Gt911Touch::Configuration>(
        I2C_NUM_0,
        540,
        960,
        false, // swapXy
        false, // mirrorX
        false, // mirrorY
        GPIO_NUM_NC, // pinReset
        GPIO_NUM_NC  // pinInterrupt
    );

    auto touch = std::make_shared<Gt911Touch>(std::move(configuration));
    return std::static_pointer_cast<tt::hal::touch::TouchDevice>(touch);
}

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay(std::shared_ptr<tt::hal::touch::TouchDevice> touch) {
    FastEpdDisplay::Configuration configuration = {
        .horizontalResolution = 540,
        .verticalResolution = 960,
        .touch = std::move(touch),
        .busSpeedHz = 20000000,
        .rotationDegrees = 90,
        .use4bppGrayscale = false,
        .fullRefreshEveryNFlushes = 40,
    };

    return std::make_shared<FastEpdDisplay>(configuration, tt::lvgl::getSyncLock());
}
