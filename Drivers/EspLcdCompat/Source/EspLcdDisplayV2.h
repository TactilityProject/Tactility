#pragma once

#include <tactility/check.h>
#include <Tactility/hal/display/DisplayDevice.h>
#include <esp_lcd_panel_dev.h>

#include <esp_lcd_types.h>
#include <esp_lvgl_port_disp.h>

constexpr auto DEFAULT_BUFFER_SIZE = 0;

struct EspLcdConfiguration {
    unsigned int horizontalResolution;
    unsigned int verticalResolution;
    int gapX;
    int gapY;
    bool monochrome;
    bool swapXY;
    bool mirrorX;
    bool mirrorY;
    bool invertColor;
    uint32_t bufferSize; // Size in pixel count. 0 means default, which is 1/10 of the screen size
    std::shared_ptr<tt::hal::touch::TouchDevice> touch;
    std::function<void(uint8_t)> _Nullable backlightDutyFunction;
    gpio_num_t resetPin;
    lv_color_format_t lvglColorFormat;
    bool lvglSwapBytes;
    lcd_rgb_element_order_t rgbElementOrder;
    uint32_t bitsPerPixel;
};

class EspLcdDisplayV2 : public tt::hal::display::DisplayDevice {
    esp_lcd_panel_io_handle_t _Nullable ioHandle = nullptr;
    esp_lcd_panel_handle_t _Nullable panelHandle = nullptr;
    lv_display_t* _Nullable lvglDisplay = nullptr;
    std::shared_ptr<tt::hal::display::DisplayDriver> _Nullable displayDriver;
    std::shared_ptr<EspLcdConfiguration> configuration;

    bool applyConfiguration() const;

    lvgl_port_display_cfg_t getLvglPortDisplayConfig(std::shared_ptr<EspLcdConfiguration> configuration, esp_lcd_panel_io_handle_t ioHandle, esp_lcd_panel_handle_t panelHandle);

protected:


    virtual bool createIoHandle(esp_lcd_panel_io_handle_t& ioHandle) = 0;

    virtual esp_lcd_panel_dev_config_t createPanelConfig(std::shared_ptr<EspLcdConfiguration> espLcdConfiguration, gpio_num_t resetPin) = 0;

    virtual bool createPanelHandle(esp_lcd_panel_io_handle_t ioHandle, const esp_lcd_panel_dev_config_t& panelConfig, esp_lcd_panel_handle_t& panelHandle) = 0;

    virtual bool isRgbPanel() const { return false; }

    virtual lvgl_port_display_rgb_cfg_t getLvglPortDisplayRgbConfig(esp_lcd_panel_io_handle_t ioHandle, esp_lcd_panel_handle_t panelHandle) { check(false, "Not supported"); }

    // Hook for MIPI-DSI DPI panels to let LVGL port use DSI-specific path
    virtual bool useDsiPanel() const { return false; }

    virtual lvgl_port_display_dsi_cfg_t getLvglPortDisplayDsiConfig(esp_lcd_panel_io_handle_t /*ioHandle*/, esp_lcd_panel_handle_t /*panelHandle*/) {
        return lvgl_port_display_dsi_cfg_t{ .flags = { .avoid_tearing = 0 } };
    }

    // Used for sending commands such as setting curves
    esp_lcd_panel_io_handle_t getIoHandle() const { return ioHandle; }

public:

    EspLcdDisplayV2(const std::shared_ptr<EspLcdConfiguration>& configuration) :
        configuration(configuration)
    {
        assert(configuration != nullptr);
    }

    ~EspLcdDisplayV2() override;

    bool start() final;

    bool stop() final;

    // region LVGL

    bool supportsLvgl() const final { return true; }

    bool startLvgl() final;

    bool stopLvgl() final;

    lv_display_t* _Nullable getLvglDisplay() const final { return lvglDisplay; }

    // endregion

    std::shared_ptr<tt::hal::touch::TouchDevice> _Nullable getTouchDevice() override { return configuration->touch; }

    // region Backlight

    void setBacklightDuty(uint8_t backlightDuty) override {
        if (configuration->backlightDutyFunction != nullptr) {
            configuration->backlightDutyFunction(backlightDuty);
        }
    }

    bool supportsBacklightDuty() const override { return configuration->backlightDutyFunction != nullptr; }

    // endregion

    // region DisplayDriver

    bool supportsDisplayDriver() const override { return true; }

    /** @return a NativeDisplay instance if this device supports it */
    std::shared_ptr<tt::hal::display::DisplayDriver> _Nullable getDisplayDriver() final;

    // endregion
};
