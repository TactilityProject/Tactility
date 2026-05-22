#include "Display.h"
#include "Sdcard.h"

#include <Tactility/hal/display/DisplayDevice.h>
#include <EspLcdDisplayDriver.h>
#include <bsp/esp32_s3_touch_amoled_2_06.h>
#include <bsp/display.h>
#include <bsp/touch.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lvgl_port.h>
#include <esp_log.h>
#include <lvgl.h>
#include <string>
#include <memory>

static const char* TAG = "Display";

static void rounder_event_cb(lv_event_t* e) {
    lv_area_t* area = (lv_area_t*)lv_event_get_param(e);
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}

class Co5300Display final : public tt::hal::display::DisplayDevice {
public:
    explicit Co5300Display(std::shared_ptr<tt::hal::touch::TouchDevice> touchDevice) : touch(std::move(touchDevice)) {}
    ~Co5300Display() override = default;

    std::string getName() const override { return "CO5300"; }
    std::string getDescription() const override { return "CO5300 AMOLED Display (SH8601)"; }

    bool start() override;
    bool stop() override;

    std::shared_ptr<tt::hal::touch::TouchDevice> getTouchDevice() override { return touch; }

    void setBacklightDuty(uint8_t backlightDuty) override;
    bool supportsBacklightDuty() const override { return true; }

    bool supportsLvgl() const override { return true; }
    bool startLvgl() override;
    bool stopLvgl() override;

    lv_display_t* getLvglDisplay() const override { return lvglDisplay; }

    bool supportsDisplayDriver() const override { return true; }
    std::shared_ptr<tt::hal::display::DisplayDriver> getDisplayDriver() override;

    void setRotation(lv_display_rotation_t rotation);

private:
    lv_display_t* lvglDisplay = nullptr;
    esp_lcd_panel_handle_t panelHandle = nullptr;
    esp_lcd_panel_io_handle_t ioHandle = nullptr;
    std::shared_ptr<tt::hal::touch::TouchDevice> touch;
    std::shared_ptr<tt::hal::display::DisplayDriver> displayDriver;
};

bool Co5300Display::start() {
    ESP_LOGI(TAG, "Starting CO5300 AMOLED display hardware");

    // SRAM optimization: max_transfer_sz = H_RES × 52 rows × 2 bytes
    int optimized_transfer_sz = BSP_LCD_H_RES * 52 * sizeof(uint16_t);
    bsp_display_config_t disp_config = {
        .max_transfer_sz = optimized_transfer_sz,
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

    return true;
}

bool Co5300Display::startLvgl() {
    ESP_LOGI(TAG, "Registering display with LVGL port");

    if (panelHandle == nullptr || ioHandle == nullptr) {
        ESP_LOGE(TAG, "Display hardware not initialized");
        return false;
    }

    constexpr int buffer_rows = 20;
    constexpr int buffer_size = BSP_LCD_H_RES * buffer_rows;

    lvgl_port_display_cfg_t disp_cfg = {};
    disp_cfg.io_handle = ioHandle;
    disp_cfg.panel_handle = panelHandle;
    disp_cfg.buffer_size = (uint32_t)buffer_size;
    disp_cfg.double_buffer = true;
    disp_cfg.hres = BSP_LCD_H_RES;
    disp_cfg.vres = BSP_LCD_V_RES;
    disp_cfg.monochrome = false;
#if LVGL_VERSION_MAJOR >= 9
    disp_cfg.color_format = LV_COLOR_FORMAT_RGB565;
#endif
    disp_cfg.flags.sw_rotate = true;
    disp_cfg.flags.buff_dma = true;
    disp_cfg.flags.buff_spiram = true;
#if LVGL_VERSION_MAJOR >= 9
    disp_cfg.flags.swap_bytes = true;
#endif

    lvglDisplay = lvgl_port_add_disp(&disp_cfg);
    if (lvglDisplay == nullptr) {
        ESP_LOGE(TAG, "Failed to add LVGL display");
        return false;
    }

    lv_display_add_event_cb(lvglDisplay, rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    ESP_LOGI(TAG, "Display registered with LVGL, lvglDisplay=%p", lvglDisplay);

    if (touch != nullptr && touch->supportsLvgl()) {
        touch->startLvgl(lvglDisplay);
    }

    return true;
}

bool Co5300Display::stopLvgl() {
    ESP_LOGI(TAG, "Stopping LVGL");

    if (touch != nullptr) {
        touch->stopLvgl();
    }

    if (lvglDisplay != nullptr) {
        lv_display_delete(lvglDisplay);
        lvglDisplay = nullptr;
    }

    return true;
}

bool Co5300Display::stop() {
    // Invalidate cached DisplayDriver - it holds a raw panelHandle that is about to be deleted
    displayDriver.reset();

    return true;
}

void Co5300Display::setBacklightDuty(uint8_t backlightDuty) {
    int brightness_percent = (backlightDuty * 100) / 255;
    bsp_display_brightness_set(brightness_percent);
}

std::shared_ptr<tt::hal::display::DisplayDriver> Co5300Display::getDisplayDriver() {
    if (lvglDisplay != nullptr) {
        ESP_LOGE(TAG, "Cannot get DisplayDriver while LVGL is active - call stopLvgl() first");
        return nullptr;
    }
    if (panelHandle == nullptr) {
        ESP_LOGE(TAG, "Cannot get DisplayDriver - display is not started");
        return nullptr;
    }
    if (displayDriver == nullptr) {
        displayDriver = std::make_shared<EspLcdDisplayDriver>(
            panelHandle,
            BSP_LCD_H_RES,
            BSP_LCD_V_RES,
            tt::hal::display::ColorFormat::RGB565Swapped
        );
    }
    return displayDriver;
}

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay(std::shared_ptr<tt::hal::touch::TouchDevice> touch) {
    return std::make_shared<Co5300Display>(std::move(touch));
}
