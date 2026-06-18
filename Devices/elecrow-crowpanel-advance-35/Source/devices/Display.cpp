#include "Display.h"

#include <Gt911Touch.h>
#include <PwmBacklight.h>
#include <Ili9488Display.h>
#include <tactility/check.h>
#include <tactility/device.h>

static std::shared_ptr<tt::hal::touch::TouchDevice> createTouch() {
    auto* i2c = device_find_by_name("i2c0");
    check(i2c);
    auto configuration = std::make_unique<Gt911Touch::Configuration>(
        i2c,
        320,
        480
    );

    return std::make_shared<Gt911Touch>(std::move(configuration));
}

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay() {
    auto touch = createTouch();

    auto configuration = std::make_unique<Ili9488Display::Configuration>(
        CROWPANEL_LCD_SPI_HOST,
        CROWPANEL_LCD_PIN_CS,
        CROWPANEL_LCD_PIN_DC,
        CROWPANEL_LCD_HORIZONTAL_RESOLUTION,
        CROWPANEL_LCD_VERTICAL_RESOLUTION,
        touch,
        false,
        false,
        false,
        true
    );

    configuration->backlightDutyFunction = driver::pwmbacklight::setBacklightDuty;

    auto display = std::make_shared<Ili9488Display>(std::move(configuration));
    return std::reinterpret_pointer_cast<tt::hal::display::DisplayDevice>(display);
}
