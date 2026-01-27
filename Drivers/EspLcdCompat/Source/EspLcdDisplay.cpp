#include "EspLcdDisplay.h"
#include "EspLcdDisplayDriver.h"

#include <Tactility/Logger.h>
#include <tactility/check.h>
#include <Tactility/hal/touch/TouchDevice.h>
#include <cassert>
#include <esp_lvgl_port_disp.h>

static const auto LOGGER = tt::Logger("EspLcdDisplay");

EspLcdDisplay::~EspLcdDisplay() {
    check(
        displayDriver == nullptr || displayDriver.use_count() < 2, // 1 reference is held by this class
        "DisplayDriver is still in use. This will cause memory access violations."
    );
}

bool EspLcdDisplay::start() {
    if (!createIoHandle(ioHandle)) {
        LOGGER.error("Failed to create IO handle");
        return false;
    }

    if (!createPanelHandle(ioHandle, panelHandle)) {
        LOGGER.error("Failed to create panel handle");
        esp_lcd_panel_io_del(ioHandle);
        return false;
    }

    return true;
}

bool EspLcdDisplay::stop() {
    if (lvglDisplay != nullptr) {
        stopLvgl();
        lvglDisplay = nullptr;
    }

    if (panelHandle != nullptr && esp_lcd_panel_del(panelHandle) != ESP_OK) {
        return false;
    }

    if (ioHandle != nullptr && esp_lcd_panel_io_del(ioHandle) != ESP_OK) {
        return false;
    }

    if (displayDriver != nullptr && displayDriver.use_count() > 1) {
        LOGGER.warn("DisplayDriver is still in use.");
    }

    return true;
}

bool EspLcdDisplay::startLvgl() {
    assert(lvglDisplay == nullptr);

    if (displayDriver != nullptr && displayDriver.use_count() > 1) {
        LOGGER.warn("DisplayDriver is still in use.");
    }

    auto lvgl_port_config  = getLvglPortDisplayConfig(ioHandle, panelHandle);

    if (isRgbPanel()) {
        auto rgb_config = getLvglPortDisplayRgbConfig(ioHandle, panelHandle);
        lvglDisplay = lvgl_port_add_disp_rgb(&lvgl_port_config , &rgb_config);
    } else {
        lvglDisplay = lvgl_port_add_disp(&lvgl_port_config );
    }

    auto touch_device = getTouchDevice();
    if (touch_device != nullptr && touch_device->supportsLvgl()) {
        touch_device->startLvgl(lvglDisplay);
    }

    return lvglDisplay != nullptr;
}

bool EspLcdDisplay::stopLvgl() {
    if (lvglDisplay == nullptr) {
        return false;
    }

    auto touch_device = getTouchDevice();
    if (touch_device != nullptr) {
        touch_device->stopLvgl();
    }

    lvgl_port_remove_disp(lvglDisplay);
    lvglDisplay = nullptr;
    return true;
}

std::shared_ptr<tt::hal::display::DisplayDriver> EspLcdDisplay::getDisplayDriver() {
    assert(lvglDisplay == nullptr); // Still attached to LVGL context. Call stopLvgl() first.
    if (displayDriver == nullptr) {
        auto lvgl_port_config  = getLvglPortDisplayConfig(ioHandle, panelHandle);

        tt::hal::display::ColorFormat color_format;
        if (lvgl_port_config.color_format == LV_COLOR_FORMAT_I1) {
            color_format = tt::hal::display::ColorFormat::Monochrome;
        } else if (lvgl_port_config.color_format == LV_COLOR_FORMAT_RGB565) {
            if (rgbElementOrder == LCD_RGB_ELEMENT_ORDER_RGB) {
                if (lvgl_port_config.flags.swap_bytes) {
                    color_format = tt::hal::display::ColorFormat::RGB565Swapped;
                } else {
                    color_format = tt::hal::display::ColorFormat::RGB565;
                }
            } else {
                if (lvgl_port_config.flags.swap_bytes) {
                    color_format = tt::hal::display::ColorFormat::BGR565Swapped;
                } else {
                    color_format = tt::hal::display::ColorFormat::BGR565;
                }
            }
        } else if (lvgl_port_config.color_format == LV_COLOR_FORMAT_RGB888) {
            color_format = tt::hal::display::ColorFormat::RGB888;
        } else {
            check(false, "unsupported driver");
        }

        displayDriver = std::make_shared<EspLcdDisplayDriver>(
            panelHandle,
            lock,
            lvgl_port_config.hres,
            lvgl_port_config.vres,
            color_format
        );
    }
    return displayDriver;
}
