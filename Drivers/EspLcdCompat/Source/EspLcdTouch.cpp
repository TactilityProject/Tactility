#include "EspLcdTouch.h"


#include <EspLcdTouchDriver.h>
#include <tactility/log.h>
#include <esp_lvgl_port_touch.h>

constexpr auto* TAG = "EspLcdTouch";

bool EspLcdTouch::start() {
    if (!createIoHandle(ioHandle) != ESP_OK) {
        LOG_E(TAG, "Touch IO failed");
        return false;
    }

    config = createEspLcdTouchConfig();

    if (!createTouchHandle(ioHandle, config, touchHandle)) {
        LOG_E(TAG, "Driver init failed");
        esp_lcd_panel_io_del(ioHandle);
        ioHandle = nullptr;
        return false;
    }

    return true;
}

bool EspLcdTouch::stop() {
    if (lvglDevice != nullptr) {
        stopLvgl();
    }

    if (ioHandle != nullptr) {
        esp_lcd_panel_io_del(ioHandle);
        ioHandle = nullptr;
    }

    if (touchHandle != nullptr) {
        esp_lcd_touch_del(touchHandle);
        touchHandle = nullptr;
    }

    return true;
}

bool EspLcdTouch::startLvgl(lv_disp_t* display) {
    if (lvglDevice != nullptr) {
        return false;
    }

    if (touchDriver != nullptr && touchDriver.use_count() > 1) {
        LOG_W(TAG, "TouchDriver is still in use.");
    }

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = display,
        .handle = touchHandle,
    };

    LOG_I(TAG, "Adding touch to LVGL");
    lvglDevice = lvgl_port_add_touch(&touch_cfg);
    if (lvglDevice == nullptr) {
        LOG_E(TAG, "Adding touch failed");
        return false;
    }

    return true;
}

bool EspLcdTouch::stopLvgl() {
    if (lvglDevice == nullptr) {
        return false;
    }

    lvgl_port_remove_touch(lvglDevice);
    lvglDevice = nullptr;

    return true;
}

std::shared_ptr<tt::hal::touch::TouchDriver> _Nullable EspLcdTouch::getTouchDriver() {
    assert(lvglDevice == nullptr); // Still attached to LVGL context. Call stopLvgl() first.

    if (touchHandle == nullptr) {
        return nullptr;
    }

    if (touchDriver == nullptr) {
        touchDriver = std::make_shared<EspLcdTouchDriver>(touchHandle);
    }

    return touchDriver;
}
