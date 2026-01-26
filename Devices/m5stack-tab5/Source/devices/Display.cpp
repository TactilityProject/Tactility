#include "Display.h"
#include "Ili9881cDisplay.h"

#include <Gt911Touch.h>
#include <PwmBacklight.h>
#include <Tactility/Logger.h>
#include <Tactility/Mutex.h>
#include <Tactility/hal/gpio/Gpio.h>

constexpr auto LCD_PIN_RESET = GPIO_NUM_0;  // Match P4 EV board reset line
constexpr auto LCD_PIN_BACKLIGHT = GPIO_NUM_22;

static std::shared_ptr<tt::hal::touch::TouchDevice> createTouch() {
    auto configuration = std::make_unique<Gt911Touch::Configuration>(
        I2C_NUM_0,
        720,
        1280,
        false,  // swapXY
        false,  // mirrorX
        false,  // mirrorY
        GPIO_NUM_NC, // reset pin
        GPIO_NUM_NC // "GPIO_NUM_23 cannot be used due to resistor to 3V3" https://github.com/espressif/esp-bsp/blob/ad668c765cbad177495a122181df0a70ff9f8f61/bsp/m5stack_tab5/src/m5stack_tab5.c#L76234
    );

    return std::make_shared<Gt911Touch>(std::move(configuration));
}

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay() {
    // Initialize PWM backlight
    if (!driver::pwmbacklight::init(LCD_PIN_BACKLIGHT, 5000, LEDC_TIMER_1, LEDC_CHANNEL_0)) {
        tt::Logger("Tab5").warn("Failed to initialize backlight");
    }

    auto touch = createTouch();

    // Work-around to init touch : interrupt pin must be set to low
    // Note: There is a resistor to 3V3 on interrupt pin which is blocking GT911 touch
    // See https://github.com/espressif/esp-bsp/blob/ad668c765cbad177495a122181df0a70ff9f8f61/bsp/m5stack_tab5/src/m5stack_tab5.c#L777
    tt::hal::gpio::configure(23, tt::hal::gpio::Mode::Output, true, false);
    tt::hal::gpio::setLevel(23, false);

    auto configuration = std::make_shared<EspLcdConfiguration>(EspLcdConfiguration {
        .horizontalResolution = 720,
        .verticalResolution = 1280,
        .gapX = 0,
        .gapY = 0,
        .monochrome = false,
        .swapXY = false,
        .mirrorX = false,
        .mirrorY = false,
        .invertColor = false,
        .bufferSize = 0, // 0 = default (1/10 of screen)
        .touch = touch,
        .backlightDutyFunction = driver::pwmbacklight::setBacklightDuty,
        .resetPin = LCD_PIN_RESET,
        .lvglColorFormat = LV_COLOR_FORMAT_RGB565,
        .lvglSwapBytes = false,
        .rgbElementOrder = LCD_RGB_ELEMENT_ORDER_RGB,
        .bitsPerPixel = 16
    });

    const auto display = std::make_shared<Ili9881cDisplay>(configuration);
    return std::static_pointer_cast<tt::hal::display::DisplayDevice>(display);
}
