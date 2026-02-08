#include "Display.h"

#include <PwmBacklight.h>
#include <St7735Display.h>

#define LCD_SPI_HOST SPI2_HOST
#define LCD_PIN_CS GPIO_NUM_4
#define LCD_PIN_DC GPIO_NUM_2
#define LCD_PIN_RESET GPIO_NUM_1
#define LCD_HORIZONTAL_RESOLUTION 80
#define LCD_VERTICAL_RESOLUTION 160
#define LCD_SPI_TRANSFER_HEIGHT (LCD_VERTICAL_RESOLUTION / 4)

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay() {
    auto configuration = std::make_unique<St7735Display::Configuration>(
        LCD_SPI_HOST,
        LCD_PIN_CS,
        LCD_PIN_DC,
        LCD_PIN_RESET,
        80,
        160,
        nullptr,
        false,
        false,
        false,
        true,
        0,
        26,
        1
    );

    configuration->backlightDutyFunction = driver::pwmbacklight::setBacklightDuty;

    auto display = std::make_shared<St7735Display>(std::move(configuration));
    return std::reinterpret_pointer_cast<tt::hal::display::DisplayDevice>(display);
}
