#include "Display.h"

#include <Gdeq031t10Display.h>

// Pins from Xinyuan-LilyGO/T-Deck-MAX's lib/TDeckMaxBoard/src/TDeckMaxBoard.h and docs/pinmap.md.
constexpr auto EPD_SPI_HOST = SPI2_HOST;
constexpr auto EPD_PIN_CS = GPIO_NUM_34;
constexpr auto EPD_PIN_DC = GPIO_NUM_35;
constexpr auto EPD_PIN_RST = GPIO_NUM_9;
constexpr auto EPD_PIN_BUSY = GPIO_NUM_37;

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay() {
    // Touch is a kernel driver (hynitron,cst66xx - see the .dts), discovered and fed to LVGL
    // independently of this display, so no touch device is passed into the configuration here.
    auto configuration = std::make_unique<Gdeq031t10Display::Configuration>(
        EPD_SPI_HOST,
        EPD_PIN_CS,
        EPD_PIN_DC,
        EPD_PIN_RST,
        EPD_PIN_BUSY,
        nullptr,
        10'000'000,
        // Default to a fast (~1s) refresh for the automatic ghost-clears so the
        // full-screen flash they cause is as short as possible.
        Gdeq031t10Display::RefreshMode::Fast
    );

    return std::make_shared<Gdeq031t10Display>(std::move(configuration));
}
