#include "Display.h"
#include "Sdcard.h"

#include <Tactility/hal/display/DisplayDevice.h>
#include <bsp/esp32_s3_touch_amoled_2_06.h>
#include <bsp/display.h>
#include <bsp/touch.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lvgl_port.h>
#include <esp_log.h>
#include <lvgl.h>
#include <string>
#include <memory>

static const char* TAG = "Display";

#ifndef BSP_LCD_H_RES
#define BSP_LCD_H_RES 410
#endif
#ifndef BSP_LCD_V_RES
#define BSP_LCD_V_RES 502
#endif

class Co5300Display final : public tt::hal::display::DisplayDevice {
public:
    explicit Co5300Display() = default;
    ~Co5300Display() override = default;

    std::string getName() const override { return "CO5300"; }
    std::string getDescription() const override { return "CO5300 AMOLED Display (SH8601)"; }

    bool start() override;
    bool stop() override;

    std::shared_ptr<tt::hal::touch::TouchDevice> getTouchDevice() override { return nullptr; }

    void setBacklightDuty(uint8_t backlightDuty) override;
    bool supportsBacklightDuty() const override { return true; }

    bool supportsLvgl() const override { return true; }
    bool startLvgl() override;
    bool stopLvgl() override { return true; }

    lv_display_t* getLvglDisplay() const override { return lvglDisplay; }

    bool supportsDisplayDriver() const override { return false; }
    std::shared_ptr<tt::hal::display::DisplayDriver> getDisplayDriver() override { return nullptr; }

private:
    lv_display_t* lvglDisplay = nullptr;
    lv_indev_t* lvglTouchIndev = nullptr;
    esp_lcd_panel_handle_t panelHandle = nullptr;
    esp_lcd_panel_io_handle_t ioHandle = nullptr;
    esp_lcd_touch_handle_t touchHandle = nullptr;
};

bool Co5300Display::start() {
    ESP_LOGI(TAG, "Starting SH8601 AMOLED display hardware");

    bsp_display_config_t disp_config = {
        .max_transfer_sz = BSP_LCD_H_RES * BSP_LCD_V_RES * 2,
    };

    esp_err_t err = bsp_display_new(&disp_config, &panelHandle, &ioHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init display hardware: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Display hardware initialized");

    esp_lcd_panel_set_gap(panelHandle, 22, 0);

    err = bsp_display_backlight_on();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to turn on backlight: %s", esp_err_to_name(err));
    }

    err = bsp_touch_new(NULL, &touchHandle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to init touch: %s", esp_err_to_name(err));
        touchHandle = nullptr;
    } else {
        ESP_LOGI(TAG, "Touch initialized");
    }

    return true;
}

bool Co5300Display::startLvgl() {
    ESP_LOGI(TAG, "Registering display with LVGL port");

    if (panelHandle == nullptr || ioHandle == nullptr) {
        ESP_LOGE(TAG, "Display hardware not initialized");
        return false;
    }

    const int buffer_height = 50;
    const int buffer_size = BSP_LCD_H_RES * buffer_height;

    lvgl_port_display_cfg_t disp_cfg = {};
    disp_cfg.io_handle = ioHandle;
    disp_cfg.panel_handle = panelHandle;
    disp_cfg.buffer_size = (uint32_t)buffer_size;
    disp_cfg.double_buffer = false;
    disp_cfg.hres = BSP_LCD_H_RES;
    disp_cfg.vres = BSP_LCD_V_RES;
    disp_cfg.monochrome = false;
    disp_cfg.rotation.swap_xy = false;
    disp_cfg.rotation.mirror_x = false;
    disp_cfg.rotation.mirror_y = false;
    disp_cfg.flags.sw_rotate = true;
#if LVGL_VERSION_MAJOR >= 9
    disp_cfg.color_format = LV_COLOR_FORMAT_RGB565;
#endif
    disp_cfg.flags.buff_dma = true;
    disp_cfg.flags.buff_spiram = false;
#if LVGL_VERSION_MAJOR >= 9
    disp_cfg.flags.swap_bytes = true;
#endif

    lvglDisplay = lvgl_port_add_disp(&disp_cfg);

    if (lvglDisplay == nullptr) {
        ESP_LOGE(TAG, "Failed to add LVGL display");
        return false;
    }

    lv_display_set_rotation(lvglDisplay, LV_DISPLAY_ROTATION_0);

    ESP_LOGI(TAG, "Display registered with LVGL, lvglDisplay=%p", lvglDisplay);

    if (touchHandle != nullptr) {
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = lvglDisplay,
            .handle = touchHandle,
        };
        lvglTouchIndev = lvgl_port_add_touch(&touch_cfg);
        if (lvglTouchIndev != nullptr) {
            ESP_LOGI(TAG, "Touch registered with LVGL");
        } else {
            ESP_LOGW(TAG, "Failed to add touch to LVGL");
        }
    }

    // Mount SD card now that SPI bus is initialized by display
    auto sdcard = tt::hal::findFirstDevice<tt::hal::sdcard::SdCardDevice>(tt::hal::Device::Type::SdCard);
    if (sdcard && sdcard->mount("/sdcard")) {
        ESP_LOGI(TAG, "SD card mounted at /sdcard");
    }

    return true;
}

bool Co5300Display::stop() {
    return true;
}

void Co5300Display::setBacklightDuty(uint8_t backlightDuty) {
    int brightness_percent = (backlightDuty * 100) / 255;
    bsp_display_brightness_set(brightness_percent);
}

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay() {
    return std::make_shared<Co5300Display>();
}