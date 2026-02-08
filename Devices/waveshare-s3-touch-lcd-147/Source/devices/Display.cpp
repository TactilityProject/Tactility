#include "Display.h"
#include "Jd9853Display.h"
#include "Axs5106Touch.h"

constexpr auto LCD_SPI_HOST = SPI2_HOST;
constexpr auto LCD_PIN_CS = GPIO_NUM_21;
constexpr auto LCD_PIN_DC = GPIO_NUM_45;
constexpr auto LCD_PIN_RESET = GPIO_NUM_40;
constexpr auto LCD_HORIZONTAL_RESOLUTION = 172;
constexpr auto LCD_VERTICAL_RESOLUTION = 320;

void setBacklightDuty(uint8_t level);

static std::shared_ptr<tt::hal::touch::TouchDevice> createTouch() {
    // Note for future changes: Reset pin is 48 and interrupt pin is 47
    auto configuration = std::make_unique<Axs5106Touch::Configuration>(
        I2C_NUM_0,
        172,
        320,
        false,
        false,
        false,
        GPIO_NUM_47,
        GPIO_NUM_48
    );

    return std::make_shared<Axs5106Touch>(std::move(configuration));
}

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay() {
    auto touch = createTouch();

    auto configuration = std::make_unique<Jd9853Display::Configuration>(
        LCD_SPI_HOST,
        LCD_PIN_CS,
        LCD_PIN_DC,
        LCD_PIN_RESET,
        172,
        320,
        touch,
        false,
        false,
        false,
        true
    );

    configuration->backlightDutyFunction = setBacklightDuty;

    auto display = std::make_shared<Jd9853Display>(std::move(configuration));
    return std::reinterpret_pointer_cast<tt::hal::display::DisplayDevice>(display);
}
