#include "Display.hpp"

#include <Gt911Touch.h>
#include <EpdiyDisplayHelper.h>


static std::shared_ptr<tt::hal::touch::TouchDevice> createTouch() {
    // Note for future changes: Interrupt pin is 48, reset is NC
    auto configuration = std::make_unique<Gt911Touch::Configuration>(
        I2C_NUM_0,
        PAPERS3_EPD_HORIZONTAL_RESOLUTION,// yMax - PaperS3 is rotated
        PAPERS3_EPD_VERTICAL_RESOLUTION, // xMax - PaperS3 is rotated
        true, // swapXY
        true, // mirrorX
        false, // mirrorY
        GPIO_NUM_NC,
        GPIO_NUM_48
    );

    return std::make_shared<Gt911Touch>(std::move(configuration));
}

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay() {
    auto touch = createTouch();
    return EpdiyDisplayHelper::createM5PaperS3Display(touch);
}
