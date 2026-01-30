#include "Display.h"

#include <PwmBacklight.h>
#include <Axs15231b/Axs15231bDisplay.h>
#include <Axs15231b/Axs15231bTouch.h>

static constexpr size_t JC3248W535C_LCD_DRAW_BUFFER_SIZE = 320 * 480;

static std::shared_ptr<tt::hal::touch::TouchDevice> createTouch() {
    auto configuration = std::make_unique<Axs15231bTouch::Configuration>(
        I2C_NUM_0,
        320,    // width
        480,    // height
        false,  // mirror_x
        false,  // mirror_y
        false   // swap_xy
    );

    return std::make_shared<Axs15231bTouch>(std::move(configuration));
}

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay() {
    auto touch = createTouch();

    auto configuration = std::make_unique<Axs15231bDisplay::Configuration>(
        SPI2_HOST,
        GPIO_NUM_45, //CS
        GPIO_NUM_NC, //DC
        GPIO_NUM_NC, //RST
        GPIO_NUM_38, //TE
        320,         // width
        480,         // height
        touch,
        false,       // swap_xy
        false,       // mirror_x
        false,       // mirror_y
        false,       // invert color
        JC3248W535C_LCD_DRAW_BUFFER_SIZE
    );

    configuration->backlightDutyFunction = driver::pwmbacklight::setBacklightDuty;

    return std::make_shared<Axs15231bDisplay>(std::move(configuration));
}
