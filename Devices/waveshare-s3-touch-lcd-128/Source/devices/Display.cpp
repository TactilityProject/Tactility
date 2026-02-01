#include "Display.h"

#include <Cst816Touch.h>
#include <PwmBacklight.h>
#include <Gc9a01Display.h>

std::shared_ptr<tt::hal::touch::TouchDevice> createTouch() {
    auto configuration = std::make_unique<Cst816sTouch::Configuration>(
        I2C_NUM_0,
        240,
        240,
        false,
        true,
        true,
        GPIO_NUM_13,
        GPIO_NUM_5
    );

    return std::make_shared<Cst816sTouch>(std::move(configuration));
}

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay() {
    auto touch = createTouch();

    auto configuration = std::make_unique<Gc9a01Display::Configuration>(
        SPI2_HOST,
        GPIO_NUM_9,
        GPIO_NUM_8,
        240,
        240,
        touch,
        false,
        false,
        true,
        true,
        0,
        LCD_RGB_ELEMENT_ORDER_BGR
    );

    configuration->resetPin = GPIO_NUM_14;
    configuration->backlightDutyFunction = driver::pwmbacklight::setBacklightDuty;

    auto display = std::make_shared<Gc9a01Display>(std::move(configuration));
    return std::reinterpret_pointer_cast<tt::hal::display::DisplayDevice>(display);
}
