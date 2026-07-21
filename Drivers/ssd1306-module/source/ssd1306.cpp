// SPDX-License-Identifier: Apache-2.0
#include <drivers/ssd1306.h>
#include <ssd1306_module.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/esp32_i2c.h>
#include <tactility/drivers/esp32_i2c_master.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <esp_err.h>
#include <esp_lcd_io_i2c.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_ssd1306.h>

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdlib>
#include <cstring>

#define TAG "SSD1306"
#define GET_CONFIG(device) (static_cast<const Ssd1306Config*>((device)->config))

struct Ssd1306Internal {
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_handle_t panel_handle;
    // Scratch buffer for the page-format transpose in ssd1306_draw_bitmap(); sized once for the
    // full panel resolution and reused every draw_bitmap() call to avoid a malloc/free per frame.
    uint8_t* page_buf;
    size_t page_buf_size;
};

static gpio_num_t pin_or_nc(const struct GpioPinSpec& pin) {
    return pin.gpio_controller == nullptr ? GPIO_NUM_NC : static_cast<gpio_num_t>(pin.pin);
}

// region Driver lifecycle

static esp_err_t create_io_handle(Device* parent, uint8_t address, esp_lcd_panel_io_handle_t* out_handle) {
    // dc_bit_offset=6/dc_low_on_data=false gives a 0x00 control byte before command bytes and
    // 0x40 before pixel data - the standard SSD1306 I2C control-byte convention. lcd_cmd_bits=0
    // means tx_param()'s own "cmd" phase is skipped entirely (see send_command() below): every
    // command byte is transmitted as if it were "data" following the correct control byte,
    // rather than a separately-framed command - this chip's protocol has no such framing to begin
    // with, unlike the multi-byte-per-command SPI/QSPI panels elsewhere in this repo.
    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = address,
        .on_color_trans_done = nullptr,
        .user_ctx = nullptr,
        .control_phase_bytes = 1,
        .dc_bit_offset = 6,
        .lcd_cmd_bits = 0,
        .lcd_param_bits = 0,
        .flags = {
            .dc_low_on_data = false,
            .disable_control_phase = false,
        },
        .scl_speed_hz = 0,
    };

    auto* parent_driver = device_get_driver(parent);
    if (driver_is_compatible(parent_driver, "espressif,esp32-i2c")) {
        auto port = static_cast<const Esp32I2cConfig*>(parent->config)->port;
        return esp_lcd_new_panel_io_i2c_v1(port, &io_config, out_handle);
    }
    if (driver_is_compatible(parent_driver, "espressif,esp32-i2c-master")) {
        auto bus = esp32_i2c_master_get_bus_handle(parent);
        io_config.scl_speed_hz = esp32_i2c_master_get_clock_frequency(parent);
        return esp_lcd_new_panel_io_i2c_v2(bus, &io_config, out_handle);
    }

    LOG_E(TAG, "Unsupported I2C driver");
    return ESP_ERR_NOT_SUPPORTED;
}

// Sends a single SSD1306 command byte: [control byte, this byte]. lcd_cmd is passed as 0 and
// ignored on the wire (see create_io_handle()'s comment) - only param/param_size actually reach
// the bus, so this doubles as the mechanism for both standalone commands (e.g. 0xAE display-off)
// and command+parameter pairs (each byte of which is just its own call to this function, matching
// how the original hand-written driver issued them as separate I2C writes).
static bool send_command(esp_lcd_panel_io_handle_t io, uint8_t command) {
    return esp_lcd_panel_io_tx_param(io, 0, &command, 1) == ESP_OK;
}

static bool send_init_sequence(esp_lcd_panel_io_handle_t io, const uint8_t* bytes, uint32_t length) {
    for (uint32_t i = 0; i < length; i++) {
        if (!send_command(io, bytes[i])) {
            LOG_E(TAG, "Failed to send init sequence byte %lu", (unsigned long)i);
            return false;
        }
    }
    return true;
}

// Manual reset with explicit timing, rather than going through esp_lcd_panel_reset(): this
// chip/config combination is known to need a specific assert/settle timing that the panel's
// standard reset (which we intentionally never wire up - reset_gpio_num is always -1 below) may
// not otherwise match. Runs before the panel handle is even created, so a subsequent init sees a
// freshly-reset chip.
static void perform_hardware_reset(const Ssd1306Config* config) {
    gpio_num_t pin = pin_or_nc(config->pin_reset);
    if (pin == GPIO_NUM_NC) {
        return;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    gpio_set_level(pin, config->reset_active_high ? 1 : 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(pin, config->reset_active_high ? 0 : 1);
    vTaskDelay(pdMS_TO_TICKS(100));
}

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    check(device_get_type(parent) == &I2C_CONTROLLER_TYPE);

    const auto* config = GET_CONFIG(device);

    auto* internal = static_cast<Ssd1306Internal*>(malloc(sizeof(Ssd1306Internal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    internal->page_buf_size = (size_t)config->horizontal_resolution * ((config->vertical_resolution + 7) / 8);
    internal->page_buf = static_cast<uint8_t*>(malloc(internal->page_buf_size));
    if (internal->page_buf == nullptr) {
        free(internal);
        return ERROR_OUT_OF_MEMORY;
    }

    // Lets a power rail gated by a gpio-hog node (asserted moments earlier, during the same
    // kernel_init() pass) stabilize before this device tries to reset/talk to the chip. See the
    // 'power-on-delay-ms' binding property.
    if (config->power_on_delay_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(config->power_on_delay_ms));
    }

    if (create_io_handle(parent, config->address, &internal->io_handle) != ESP_OK) {
        LOG_E(TAG, "Failed to create panel IO");
        free(internal->page_buf);
        free(internal);
        return ERROR_RESOURCE;
    }

    perform_hardware_reset(config);

    esp_lcd_panel_ssd1306_config_t ssd1306_config = {
        .height = static_cast<uint8_t>(config->vertical_resolution),
    };

    // color_space (not rgb_ele_order): this deprecated union member is the only one whose type
    // can express ESP_LCD_COLOR_SPACE_MONOCHROME - rgb_ele_order's own enum only covers RGB/BGR.
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1, // always -1: reset is handled manually above, not by the panel itself
        .color_space = ESP_LCD_COLOR_SPACE_MONOCHROME,
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
        .bits_per_pixel = 1,
        .flags = { .reset_active_high = false },
        .vendor_config = &ssd1306_config,
    };

    if (esp_lcd_new_panel_ssd1306(internal->io_handle, &panel_config, &internal->panel_handle) != ESP_OK) {
        LOG_E(TAG, "Failed to create panel");
        esp_lcd_panel_io_del(internal->io_handle);
        free(internal->page_buf);
        free(internal);
        return ERROR_RESOURCE;
    }

    // Bring-up sequence. Every failure path below must clean up fully: unlike stop_device, this
    // is never retried by the kernel if start_device fails (see device_start() in
    // TactilityKernel), so a partial failure here would leak.
    bool ok;
    if (config->init_sequence != nullptr && config->init_sequence_length > 0) {
        ok = send_init_sequence(internal->io_handle, config->init_sequence, config->init_sequence_length);
    } else {
        ok = esp_lcd_panel_init(internal->panel_handle) == ESP_OK;
    }
    ok = ok && (!config->invert_color || esp_lcd_panel_invert_color(internal->panel_handle, true) == ESP_OK);
    ok = ok && esp_lcd_panel_disp_on_off(internal->panel_handle, true) == ESP_OK;

    if (!ok) {
        LOG_E(TAG, "Failed to bring up panel");
        esp_lcd_panel_del(internal->panel_handle);
        esp_lcd_panel_io_del(internal->io_handle);
        free(internal->page_buf);
        free(internal);
        return ERROR_RESOURCE;
    }

    vTaskDelay(pdMS_TO_TICKS(100)); // Let the panel stabilize before the first real draw_bitmap()

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Ssd1306Internal*>(device_get_driver_data(device));

    if (internal->panel_handle != nullptr) {
        if (esp_lcd_panel_del(internal->panel_handle) != ESP_OK) {
            LOG_E(TAG, "Failed to delete panel");
            return ERROR_RESOURCE;
        }
        internal->panel_handle = nullptr;
    }

    if (internal->io_handle != nullptr) {
        if (esp_lcd_panel_io_del(internal->io_handle) != ESP_OK) {
            LOG_E(TAG, "Failed to delete panel IO");
            return ERROR_RESOURCE;
        }
        internal->io_handle = nullptr;
    }

    free(internal->page_buf);
    free(internal);
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

// endregion

// region DisplayApi

static error_t ssd1306_reset(Device* device) {
    perform_hardware_reset(GET_CONFIG(device));
    return ERROR_NONE;
}

static error_t ssd1306_init(Device* device) {
    auto* internal = static_cast<Ssd1306Internal*>(device_get_driver_data(device));
    const auto* config = GET_CONFIG(device);
    bool ok;
    if (config->init_sequence != nullptr && config->init_sequence_length > 0) {
        ok = send_init_sequence(internal->io_handle, config->init_sequence, config->init_sequence_length);
    } else {
        ok = esp_lcd_panel_init(internal->panel_handle) == ESP_OK;
    }
    return ok ? ERROR_NONE : ERROR_RESOURCE;
}

// DISPLAY_COLOR_FORMAT_MONOCHROME's wire format (matching LVGL's LV_COLOR_FORMAT_I1, palette
// stripped) is row-major: each byte packs 8 horizontally-adjacent pixels, MSB = leftmost. The
// SSD1306's GDDRAM is page-addressed instead: each byte packs 8 *vertically* adjacent pixels of
// one column, LSB = topmost row of the page. Transpose one into the other before handing off to
// the vendor component, which expects page format directly.
static void transpose_row_major_to_pages(const uint8_t* src, int32_t width, int32_t height, uint8_t* out) {
    uint32_t row_stride = (uint32_t)(width + 7) / 8;
    for (int32_t y = 0; y < height; y++) {
        uint8_t page = (uint8_t)(y / 8);
        uint8_t bit_in_page = (uint8_t)(y % 8);
        for (int32_t x = 0; x < width; x++) {
            uint8_t src_byte = src[y * row_stride + x / 8];
            uint8_t pixel = (src_byte >> (7 - (x % 8))) & 0x01;
            out[page * width + x] |= (uint8_t)(pixel << bit_in_page);
        }
    }
}

static error_t ssd1306_draw_bitmap(Device* device, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data) {
    auto* internal = static_cast<Ssd1306Internal*>(device_get_driver_data(device));
    int32_t width = x_end - x_start;
    int32_t height = y_end - y_start;
    size_t needed = (size_t)width * ((height + 7) / 8);
    check(needed <= internal->page_buf_size);

    memset(internal->page_buf, 0, needed);
    transpose_row_major_to_pages(static_cast<const uint8_t*>(color_data), width, height, internal->page_buf);

    return esp_lcd_panel_draw_bitmap(internal->panel_handle, x_start, y_start, x_end, y_end, internal->page_buf) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

// mirror/swap_xy/set_gap are not exposed: the SSD1306 vendor component doesn't implement them.

static error_t ssd1306_invert_color(Device* device, bool invert_color_data) {
    auto* internal = static_cast<Ssd1306Internal*>(device_get_driver_data(device));
    return esp_lcd_panel_invert_color(internal->panel_handle, invert_color_data) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t ssd1306_disp_on_off(Device* device, bool on_off) {
    auto* internal = static_cast<Ssd1306Internal*>(device_get_driver_data(device));
    return esp_lcd_panel_disp_on_off(internal->panel_handle, on_off) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

// disp_sleep is not exposed: the SSD1306 vendor component doesn't implement it either (display
// on/off via 0xAE/0xAF is this chip's equivalent, already covered by disp_on_off above).

static enum DisplayColorFormat ssd1306_get_color_format(Device*) {
    return DISPLAY_COLOR_FORMAT_MONOCHROME;
}

static uint16_t ssd1306_get_resolution_x(Device* device) {
    return GET_CONFIG(device)->horizontal_resolution;
}

static uint16_t ssd1306_get_resolution_y(Device* device) {
    return GET_CONFIG(device)->vertical_resolution;
}

static void ssd1306_get_frame_buffer(Device*, uint8_t, void** out_buffer) {
    *out_buffer = nullptr;
}

static uint8_t ssd1306_get_frame_buffer_count(Device*) {
    return 0;
}

// get_backlight is not exposed: SSD1306 is a self-emitting OLED panel with no backlight.

// endregion

static const DisplayApi ssd1306_display_api = {
    .capabilities = DISPLAY_CAPABILITY_INVERT_COLOR | DISPLAY_CAPABILITY_ON_OFF,
    .reset = ssd1306_reset,
    .init = ssd1306_init,
    .draw_bitmap = ssd1306_draw_bitmap,
    .mirror = nullptr,
    .swap_xy = nullptr,
    .get_swap_xy = nullptr,
    .get_mirror_x = nullptr,
    .get_mirror_y = nullptr,
    .set_gap = nullptr,
    .invert_color = ssd1306_invert_color,
    .disp_on_off = ssd1306_disp_on_off,
    .disp_sleep = nullptr,
    .get_color_format = ssd1306_get_color_format,
    .get_resolution_x = ssd1306_get_resolution_x,
    .get_resolution_y = ssd1306_get_resolution_y,
    .get_frame_buffer = ssd1306_get_frame_buffer,
    .get_frame_buffer_count = ssd1306_get_frame_buffer_count,
    .get_backlight = nullptr,
    .has_capability = nullptr,
};

Driver ssd1306_driver = {
    .name = "ssd1306",
    .compatible = (const char*[]) { "solomon,ssd1306", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &ssd1306_display_api,
    .device_type = &DISPLAY_TYPE,
    .owner = &ssd1306_module,
    .internal = nullptr
};
