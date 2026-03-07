#include <Xpt2046SoftSpi.h>
#include <Tactility/hal/touch/TouchDevice.h>

#include "Display.h"
#include "PwmBacklight.h"
#include "St7789i8080Display.h"

static bool touchSpiInitialized = false;

static std::shared_ptr<tt::hal::touch::TouchDevice> createTouch() {
    auto config = std::make_unique<Xpt2046SoftSpi::Configuration>(
        TOUCH_MOSI_PIN,
        TOUCH_MISO_PIN,
        TOUCH_SCK_PIN,
        TOUCH_CS_PIN,
        DISPLAY_HORIZONTAL_RESOLUTION,
        DISPLAY_VERTICAL_RESOLUTION
    );

    return std::make_shared<Xpt2046SoftSpi>(std::move(config));
}

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay() {
    // Create configuration
    auto config = St7789i8080Display::Configuration(
        DISPLAY_CS,   // CS
        DISPLAY_DC,   // DC
        DISPLAY_WR,   // WR
        DISPLAY_RD,   // RD
        { DISPLAY_I80_D0, DISPLAY_I80_D1, DISPLAY_I80_D2, DISPLAY_I80_D3,
          DISPLAY_I80_D4, DISPLAY_I80_D5, DISPLAY_I80_D6, DISPLAY_I80_D7 }, // D0..D7
        DISPLAY_RST,   // RST
        DISPLAY_BL   // BL
    );
    
    // Set resolution explicitly
    config.horizontalResolution = DISPLAY_HORIZONTAL_RESOLUTION;
    config.verticalResolution = DISPLAY_VERTICAL_RESOLUTION;
    config.backlightDutyFunction = driver::pwmbacklight::setBacklightDuty;
    config.touch = createTouch();
    config.invertColor = false;
    
    auto display = std::make_shared<St7789i8080Display>(config);
    return display;
}
