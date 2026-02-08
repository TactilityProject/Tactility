#pragma once

#include <Tactility/Lock.h>
#include <tactility/check.h>
#include <Tactility/hal/display/DisplayDevice.h>

#include <esp_lcd_types.h>
#include <esp_lvgl_port_disp.h>

/** @deprecated use EspLcdDisplayV2 */
class EspLcdDisplay : public tt::hal::display::DisplayDevice {

    esp_lcd_panel_io_handle_t ioHandle = nullptr;
    esp_lcd_panel_handle_t panelHandle = nullptr;
    lv_display_t* lvglDisplay = nullptr;
    std::shared_ptr<tt::hal::display::DisplayDriver> displayDriver;
    lcd_rgb_element_order_t rgbElementOrder;

protected:

    // Used for sending commands such as setting curves
    esp_lcd_panel_io_handle_t getIoHandle() const { return ioHandle; }

    virtual bool createIoHandle(esp_lcd_panel_io_handle_t& outHandle) = 0;

    virtual bool createPanelHandle(esp_lcd_panel_io_handle_t ioHandle, esp_lcd_panel_handle_t& panelHandle) = 0;

    virtual lvgl_port_display_cfg_t getLvglPortDisplayConfig(esp_lcd_panel_io_handle_t ioHandle, esp_lcd_panel_handle_t panelHandle) = 0;

    virtual bool isRgbPanel() const { return false; }

    virtual lvgl_port_display_rgb_cfg_t getLvglPortDisplayRgbConfig(esp_lcd_panel_io_handle_t ioHandle, esp_lcd_panel_handle_t panelHandle) { check(false, "Not supported"); }

public:

    EspLcdDisplay() = default;

    ~EspLcdDisplay() override;

    bool start() final;

    bool stop() final;

    // region LVGL

    bool supportsLvgl() const final { return true; }

    bool startLvgl() final;

    bool stopLvgl() final;

    lv_display_t* getLvglDisplay() const final { return lvglDisplay; }

    // endregion

    // region DisplayDriver

    bool supportsDisplayDriver() const override { return true; }

    /** @return a NativeDisplay instance if this device supports it */
    std::shared_ptr<tt::hal::display::DisplayDriver> getDisplayDriver() final;

    // endregion
};
