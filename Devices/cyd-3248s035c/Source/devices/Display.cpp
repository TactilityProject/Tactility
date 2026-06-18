#include "Display.h"

#include <Gt911Touch.h>
#include <PwmBacklight.h>
#include <St7796Display.h>
#include <tactility/check.h>
#include <tactility/device.h>

static std::shared_ptr<tt::hal::touch::TouchDevice> createTouch() {
    auto* i2c = device_find_by_name("i2c0");
    check(i2c);
    auto configuration = std::make_unique<Gt911Touch::Configuration>(
        i2c,
        LCD_HORIZONTAL_RESOLUTION,
        LCD_VERTICAL_RESOLUTION
    );

    return std::make_shared<Gt911Touch>(std::move(configuration));
}

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay() {
    auto touch = createTouch();
    auto configuration = std::make_unique<St7796Display::Configuration>(
        LCD_SPI_HOST,
        LCD_PIN_CS,
        LCD_PIN_DC,
        LCD_HORIZONTAL_RESOLUTION,
        LCD_VERTICAL_RESOLUTION,
        touch,
        false,
        true,
        false,
        false
    );

    configuration->backlightDutyFunction = driver::pwmbacklight::setBacklightDuty;

    auto display = std::make_shared<St7796Display>(std::move(configuration));
    return std::reinterpret_pointer_cast<tt::hal::display::DisplayDevice>(display);
}
