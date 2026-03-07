#include "Display.h"
#include <Gt911Touch.h>
#include <EpdiyDisplayHelper.h>

std::shared_ptr<tt::hal::touch::TouchDevice> createTouch() {
    auto configuration = std::make_unique<Gt911Touch::Configuration>(
        I2C_NUM_0,
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
