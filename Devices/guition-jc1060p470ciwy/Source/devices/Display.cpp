#include "Display.h"
#include "Jd9165Display.h"

#include <Gt911Touch.h>
#include <PwmBacklight.h>
#include <Tactility/Mutex.h>
#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/log.h>

constexpr auto LCD_PIN_RESET = GPIO_NUM_0;  // Match P4 EV board reset line
constexpr auto LCD_PIN_BACKLIGHT = GPIO_NUM_23;
constexpr auto LCD_HORIZONTAL_RESOLUTION = 1024;
constexpr auto LCD_VERTICAL_RESOLUTION = 600;

constexpr auto TOUCH_PIN_RESET = GPIO_NUM_NC;
constexpr auto TOUCH_PIN_INTERRUPT = GPIO_NUM_NC;

static std::shared_ptr<tt::hal::touch::TouchDevice> createTouch() {
    auto* i2c = device_find_by_name("i2c_internal");
    check(i2c);
    auto configuration = std::make_unique<Gt911Touch::Configuration>(
        i2c,
        LCD_HORIZONTAL_RESOLUTION,
        LCD_VERTICAL_RESOLUTION,
        false,  // swapXY
        false,  // mirrorX
        false,  // mirrorY
        TOUCH_PIN_RESET,
        TOUCH_PIN_INTERRUPT
    );

    return std::make_shared<Gt911Touch>(std::move(configuration));
}

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay() {
    // Initialize PWM backlight
    if (!driver::pwmbacklight::init(LCD_PIN_BACKLIGHT, 20000, LEDC_TIMER_1, LEDC_CHANNEL_0)) {
        LOG_W("jc1060p470ciwy", "Failed to initialize backlight");
    }

    auto touch = createTouch();

    auto configuration = std::make_shared<EspLcdConfiguration>(EspLcdConfiguration {
        .horizontalResolution = LCD_HORIZONTAL_RESOLUTION,
        .verticalResolution = LCD_VERTICAL_RESOLUTION,
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

    const auto display = std::make_shared<Jd9165Display>(configuration);
    return std::static_pointer_cast<tt::hal::display::DisplayDevice>(display);
}
