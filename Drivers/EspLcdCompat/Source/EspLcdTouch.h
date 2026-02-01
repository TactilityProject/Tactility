#pragma once

#include <esp_lcd_touch.h>
#include <esp_lcd_types.h>
#include <lvgl.h>
#include <Tactility/hal/touch/TouchDevice.h>
#include <Tactility/hal/touch/TouchDriver.h>

class EspLcdTouch : public tt::hal::touch::TouchDevice {

    esp_lcd_touch_config_t config;
    esp_lcd_panel_io_handle_t _Nullable ioHandle = nullptr;
    esp_lcd_touch_handle_t _Nullable touchHandle = nullptr;
    lv_indev_t* lvglDevice = nullptr;
    std::shared_ptr<tt::hal::touch::TouchDriver> touchDriver;

protected:

    esp_lcd_touch_handle_t _Nullable getTouchHandle() const { return touchHandle; }

    virtual bool createIoHandle(esp_lcd_panel_io_handle_t& ioHandle) = 0;

    virtual bool createTouchHandle(esp_lcd_panel_io_handle_t ioHandle, const esp_lcd_touch_config_t& configuration, esp_lcd_touch_handle_t& touchHandle) = 0;

    virtual esp_lcd_touch_config_t createEspLcdTouchConfig() = 0;

public:

    bool start() final;

    bool stop() final;

    bool supportsLvgl() const final { return true; }

    bool startLvgl(lv_display_t* display) final;

    bool stopLvgl() final;

    lv_indev_t* getLvglIndev() final { return lvglDevice; }

    bool supportsTouchDriver() override { return true; }

    std::shared_ptr<tt::hal::touch::TouchDriver> _Nullable getTouchDriver() final;
};
