#include "Touch.h"

#include <bsp/display.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_touch_ft5x06.h>
#include <esp_lvgl_port.h>
#include <esp_log.h>
#include <EspLcdTouchDriver.h>

#include <tactility/device.h>
#include <tactility/drivers/esp32_i2c.h>

static const char* TAG = "Touch";

// FT5x06 register: delay in seconds before entering monitor/sleep mode.
// LVGL reads touch at 30-60Hz but GraphicsDemo reads only once per ~13s frame,
// causing the controller to sleep and NAK I2C reads. Set to 0 to never sleep.
#define FT5x06_REG_TIME_ENTER_MONITOR 0x87

class Co5300Touch final : public tt::hal::touch::TouchDevice {
public:
    explicit Co5300Touch() = default;
    ~Co5300Touch() override = default;

    std::string getName() const override { return "CO5300 Touch"; }
    std::string getDescription() const override { return "CO5300 AMOLED Touch (FT3168)"; }

    bool start() override;
    bool stop() override;

    bool supportsLvgl() const override { return true; }
    bool startLvgl(lv_display_t* display) override;
    bool stopLvgl() override;

    lv_indev_t* getLvglIndev() override { return lvglTouchIndev; }

    bool supportsTouchDriver() override { return true; }
    std::shared_ptr<tt::hal::touch::TouchDriver> getTouchDriver() override;

private:
    esp_lcd_touch_handle_t touchHandle = nullptr;
    esp_lcd_panel_io_handle_t ioHandle = nullptr;
    lv_indev_t* lvglTouchIndev = nullptr;
    std::shared_ptr<tt::hal::touch::TouchDriver> touchDriver;
    void* lvglTouchCtx = nullptr;
};

static ::Device* findI2C() {
    ::Device* i2c = device_find_by_name("i2c0");
    if (i2c) return i2c;
    i2c = device_find_by_name("i2c_internal");
    if (i2c) return i2c;
    return nullptr;
}

bool Co5300Touch::start() {
    ESP_LOGI(TAG, "Starting CO5300 touch hardware");

    ::Device* i2c_dev = findI2C();
    if (!i2c_dev) {
        ESP_LOGE(TAG, "I2C controller not found");
        return false;
    }

    if (!device_is_ready(i2c_dev)) {
        ESP_LOGE(TAG, "I2C controller not ready");
        return false;
    }

    auto* bus_handle = esp32_i2c_get_bus_handle(i2c_dev);
    if (!bus_handle) {
        ESP_LOGE(TAG, "I2C bus handle not found");
        return false;
    }

    esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    io_config.scl_speed_hz = 400000;
    esp_err_t err = esp_lcd_new_panel_io_i2c(bus_handle, &io_config, &ioHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create panel IO: %s", esp_err_to_name(err));
        return false;
    }

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = BSP_LCD_H_RES,
        .y_max = BSP_LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_9,
        .int_gpio_num = GPIO_NUM_38,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
        .process_coordinates = nullptr,
        .interrupt_callback = nullptr,
        .user_data = nullptr,
        .driver_data = nullptr,
    };
    err = esp_lcd_touch_new_i2c_ft5x06(ioHandle, &tp_cfg, &touchHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init touch: %s", esp_err_to_name(err));
        esp_lcd_panel_io_del(ioHandle);
        ioHandle = nullptr;
        return false;
    }
    ESP_LOGI(TAG, "Touch initialized");

    // Disable the monitor/sleep timeout so that infrequent reads
    // (e.g. from app drivers bypassing LVGL) don't NAK I2C transactions
    uint8_t disable_monitor = 0;
    err = esp_lcd_panel_io_tx_param(touchHandle->io, FT5x06_REG_TIME_ENTER_MONITOR, &disable_monitor, 1);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to disable monitor timeout: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Touch monitor timeout disabled");
    }

    // Test touch read right after init
    err = esp_lcd_touch_read_data(touchHandle);
    if (err == ESP_OK) {
        uint16_t x, y;
        uint8_t cnt;
        bool touched = esp_lcd_touch_get_coordinates(touchHandle, &x, &y, NULL, &cnt, 1);
        ESP_LOGI(TAG, "Test read: %s (cnt=%d)", touched ? "touched" : "not touched", cnt);
    } else {
        ESP_LOGW(TAG, "Test read_data failed: %s", esp_err_to_name(err));
    }

    return true;
}

bool Co5300Touch::stop() {
    if (lvglTouchIndev != nullptr) {
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

    lvglTouchCtx = nullptr;
    touchDriver.reset();
    return true;
}

bool Co5300Touch::startLvgl(lv_display_t* display) {
    if (touchHandle == nullptr || display == nullptr) {
        return false;
    }
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = display,
        .handle = touchHandle,
    };
    lvglTouchIndev = lvgl_port_add_touch(&touch_cfg);
    if (lvglTouchIndev != nullptr) {
        lvglTouchCtx = (void*)lv_indev_get_driver_data(lvglTouchIndev);
        ESP_LOGI(TAG, "Touch registered with LVGL");
        return true;
    } else {
        ESP_LOGW(TAG, "Failed to add touch to LVGL");
        return false;
    }
}

bool Co5300Touch::stopLvgl() {
    if (lvglTouchIndev == nullptr) {
        return false;
    }

    // Unregister interrupt callback (same as lvgl_port_remove_touch internals)
    if (touchHandle != nullptr && touchHandle->config.int_gpio_num != GPIO_NUM_NC) {
        esp_lcd_touch_register_interrupt_callback(touchHandle, NULL);
    }

    // Free allocated touch context
    if (lvglTouchCtx != nullptr) {
        free(lvglTouchCtx);
        lvglTouchCtx = nullptr;
    }

    lv_indev_delete(lvglTouchIndev);
    lvglTouchIndev = nullptr;

    return true;
}

std::shared_ptr<tt::hal::touch::TouchDriver> Co5300Touch::getTouchDriver() {
    assert(lvglTouchIndev == nullptr); // Still attached to LVGL context. Call stopLvgl() first.

    if (touchHandle == nullptr) {
        return nullptr;
    }

    if (touchDriver == nullptr) {
        touchDriver = std::make_shared<EspLcdTouchDriver>(touchHandle);
    }
    return touchDriver;
}

std::shared_ptr<tt::hal::touch::TouchDevice> createTouch() {
    return std::make_shared<Co5300Touch>();
}
