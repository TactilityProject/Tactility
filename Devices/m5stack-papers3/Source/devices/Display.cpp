#include "Display.h"
#include <Gt911Touch.h>
#include <EpdiyDisplayHelper.h>
#include <tactility/check.h>
#include <tactility/device.h>

std::shared_ptr<tt::hal::touch::TouchDevice> createTouch() {
    auto* i2c = device_find_by_name("i2c_internal");
    check(i2c);
    auto configuration = std::make_unique<Gt911Touch::Configuration>(
        i2c,
        540,
        960,
        true,  // swapXy
        true,  // mirrorX
        false, // mirrorY
        GPIO_NUM_NC, // pinReset
        GPIO_NUM_NC  //48 pinInterrupt
    );

    return std::make_shared<Gt911Touch>(std::move(configuration));
}

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay() {
    auto touch = createTouch();
    return EpdiyDisplayHelper::createM5PaperS3Display(touch);
}
