// SPDX-License-Identifier: Apache-2.0
#include <drivers/st7789_i8080.h>
#include <st7789_i8080_module.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/esp32_i8080.h>
#include <tactility/drivers/i8080_controller.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <esp_err.h>
#include <esp_lcd_panel_commands.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_st7789.h>

#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdlib>

#define TAG "ST7789I8080"
#define GET_CONFIG(device) (static_cast<const St7789I8080Config*>((device)->config))

// Maps gamma-curve devicetree index [0,3] to the MIPI DCS GAMSET (0x26) parameter value. Mirrors
// the deprecated HAL's EspLcdSpiDisplay::setGammaCurve() (Drivers/EspLcdCompat) - note the
// non-linear mapping, not index+1.
static const uint8_t GAMMA_CURVE_VALUES[4] = { 0x01, 0x04, 0x02, 0x08 };

namespace {

// Panel bring-up commands beyond esp_lcd_panel_init()'s generic ST7789 sequence: gate/porch/power
// control and gamma curves. Kept from the deprecated HAL's St7789i8080Display driver verbatim -
// likely panel-specific tuning for the exact glass used on LilyGO T-HMI boards, not guaranteed
// redundant with esp_lcd's built-in defaults, so preserved rather than dropped.
struct LcdInitCmd {
    uint8_t cmd;
    uint8_t data[14];
    uint8_t len;
};

constexpr LcdInitCmd ST7789_INIT_CMDS[] = {
    {0x36, {0x08}, 1},
    {0x3A, {0x05}, 1},
    {0x20, {0}, 0},
    {0xB2, {0x0B, 0x0B, 0x00, 0x33, 0x33}, 5},
    {0xB7, {0x75}, 1},
    {0xBB, {0x28}, 1},
    {0xC0, {0x2C}, 1},
    {0xC2, {0x01}, 1},
    {0xC3, {0x1F}, 1},
    {0xC6, {0x13}, 1},
    {0xD0, {0xA7}, 1},
    {0xD0, {0xA4, 0xA1}, 2},
    {0xD6, {0xA1}, 1},
    {0xE0, {0xF0, 0x05, 0x0A, 0x06, 0x06, 0x03, 0x2B, 0x32, 0x43, 0x36, 0x11, 0x10, 0x2B, 0x32}, 14},
    {0xE1, {0xF0, 0x08, 0x0C, 0x0B, 0x09, 0x24, 0x2B, 0x22, 0x43, 0x38, 0x15, 0x16, 0x2F, 0x37}, 14},
};

} // namespace

struct St7789I8080Internal {
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_handle_t panel_handle;
    // See Ili9341Internal's identical field in ili9341-module for why this exists: draw_bitmap()
    // must block until the transfer physically completes (DisplayApi's synchronous contract).
    SemaphoreHandle_t draw_done_semaphore;
};

static bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void* user_ctx) {
    auto* internal = static_cast<St7789I8080Internal*>(user_ctx);
    BaseType_t high_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(internal->draw_done_semaphore, &high_task_woken);
    return high_task_woken == pdTRUE;
}

static int pin_or_unused(const struct GpioPinSpec& pin) {
    return pin.gpio_controller == nullptr ? -1 : static_cast<int>(pin.pin);
}

// region Driver lifecycle

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    check(device_get_type(parent) == &I8080_CONTROLLER_TYPE);

    esp_lcd_i80_bus_handle_t bus_handle;
    if (esp32_i8080_get_bus_handle(device, &bus_handle) != ERROR_NONE) {
        LOG_E(TAG, "Failed to resolve i80 bus handle");
        return ERROR_RESOURCE;
    }

    struct GpioPinSpec cs_pin;
    if (esp32_i8080_get_cs_pin(device, &cs_pin) != ERROR_NONE) {
        LOG_E(TAG, "Failed to resolve CS pin");
        return ERROR_RESOURCE;
    }

    const auto* config = GET_CONFIG(device);

    auto* internal = static_cast<St7789I8080Internal*>(malloc(sizeof(St7789I8080Internal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    internal->draw_done_semaphore = xSemaphoreCreateBinary();
    if (internal->draw_done_semaphore == nullptr) {
        free(internal);
        return ERROR_OUT_OF_MEMORY;
    }

    esp_lcd_panel_io_i80_config_t io_config = {
        .cs_gpio_num = pin_or_unused(cs_pin),
        .pclk_hz = config->pixel_clock_hz,
        .trans_queue_depth = config->transaction_queue_depth,
        .on_color_trans_done = on_color_trans_done,
        .user_ctx = internal,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .dc_levels = {
            .dc_idle_level = 0,
            .dc_cmd_level = 0,
            .dc_dummy_level = 0,
            .dc_data_level = 1,
        },
        .flags = {
            .cs_active_high = 0,
            .reverse_color_bits = 0,
            .swap_color_bytes = 1,
            .pclk_active_neg = 0,
            .pclk_idle_low = 0,
        },
    };

    esp_err_t ret = esp_lcd_new_panel_io_i80(bus_handle, &io_config, &internal->io_handle);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to create panel IO: %s", esp_err_to_name(ret));
        vSemaphoreDelete(internal->draw_done_semaphore);
        free(internal);
        return ERROR_RESOURCE;
    }

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = pin_or_unused(config->pin_reset),
        .rgb_ele_order = config->bgr_order ? LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
        .flags = { .reset_active_high = config->reset_active_high },
        .vendor_config = nullptr,
    };

    ret = esp_lcd_new_panel_st7789(internal->io_handle, &panel_config, &internal->panel_handle);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to create panel: %s", esp_err_to_name(ret));
        esp_lcd_panel_io_del(internal->io_handle);
        vSemaphoreDelete(internal->draw_done_semaphore);
        free(internal);
        return ERROR_RESOURCE;
    }

    // Bring-up sequence, order matches the deprecated HAL's St7789i8080Display (proven correct on
    // real T-HMI hardware). Every failure path below must clean up fully, same reasoning as
    // ili9341-module's start().
    bool ok =
        esp_lcd_panel_reset(internal->panel_handle) == ESP_OK &&
        esp_lcd_panel_init(internal->panel_handle) == ESP_OK;

    if (ok) {
        for (const auto& init_cmd : ST7789_INIT_CMDS) {
            if (esp_lcd_panel_io_tx_param(internal->io_handle, init_cmd.cmd, init_cmd.data, init_cmd.len) != ESP_OK) {
                ok = false;
                break;
            }
        }
    }

    if (ok) {
        int gap_x = config->swap_xy ? config->gap_y : config->gap_x;
        int gap_y = config->swap_xy ? config->gap_x : config->gap_y;
        ok = (gap_x == 0 && gap_y == 0) || esp_lcd_panel_set_gap(internal->panel_handle, gap_x, gap_y) == ESP_OK;
    }
    ok = ok && (!config->swap_xy || esp_lcd_panel_swap_xy(internal->panel_handle, true) == ESP_OK);
    ok = ok && ((!config->mirror_x && !config->mirror_y) || esp_lcd_panel_mirror(internal->panel_handle, config->mirror_x, config->mirror_y) == ESP_OK);
    ok = ok && (!config->invert_color || esp_lcd_panel_invert_color(internal->panel_handle, true) == ESP_OK);
    ok = ok && (config->gamma_curve >= 4 || esp_lcd_panel_io_tx_param(internal->io_handle, LCD_CMD_GAMSET, &GAMMA_CURVE_VALUES[config->gamma_curve], 1) == ESP_OK);
    ok = ok && esp_lcd_panel_disp_on_off(internal->panel_handle, true) == ESP_OK;

    if (!ok) {
        LOG_E(TAG, "Failed to bring up panel");
        esp_lcd_panel_del(internal->panel_handle);
        esp_lcd_panel_io_del(internal->io_handle);
        vSemaphoreDelete(internal->draw_done_semaphore);
        free(internal);
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<St7789I8080Internal*>(device_get_driver_data(device));

    error_t result = ERROR_NONE;

    if (esp_lcd_panel_del(internal->panel_handle) != ESP_OK) {
        LOG_E(TAG, "Failed to delete panel");
        result = ERROR_RESOURCE;
    }
    if (esp_lcd_panel_io_del(internal->io_handle) != ESP_OK) {
        LOG_E(TAG, "Failed to delete panel IO");
        result = ERROR_RESOURCE;
    }

    vSemaphoreDelete(internal->draw_done_semaphore);
    free(internal);
    return result;
}

// endregion

// region DisplayApi

static error_t st7789_i8080_reset(Device* device) {
    auto* internal = static_cast<St7789I8080Internal*>(device_get_driver_data(device));
    return esp_lcd_panel_reset(internal->panel_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t st7789_i8080_init(Device* device) {
    auto* internal = static_cast<St7789I8080Internal*>(device_get_driver_data(device));
    return esp_lcd_panel_init(internal->panel_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t st7789_i8080_draw_bitmap(Device* device, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data) {
    auto* internal = static_cast<St7789I8080Internal*>(device_get_driver_data(device));

    xSemaphoreTake(internal->draw_done_semaphore, 0);

    if (esp_lcd_panel_draw_bitmap(internal->panel_handle, x_start, y_start, x_end, y_end, color_data) != ESP_OK) {
        return ERROR_RESOURCE;
    }

    xSemaphoreTake(internal->draw_done_semaphore, portMAX_DELAY);
    return ERROR_NONE;
}

static error_t st7789_i8080_mirror(Device* device, bool x_axis, bool y_axis) {
    auto* internal = static_cast<St7789I8080Internal*>(device_get_driver_data(device));
    return esp_lcd_panel_mirror(internal->panel_handle, x_axis, y_axis) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t st7789_i8080_swap_xy(Device* device, bool swap_axes) {
    auto* internal = static_cast<St7789I8080Internal*>(device_get_driver_data(device));
    return esp_lcd_panel_swap_xy(internal->panel_handle, swap_axes) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static bool st7789_i8080_get_swap_xy(Device* device) {
    return GET_CONFIG(device)->swap_xy;
}

static bool st7789_i8080_get_mirror_x(Device* device) {
    return GET_CONFIG(device)->mirror_x;
}

static bool st7789_i8080_get_mirror_y(Device* device) {
    return GET_CONFIG(device)->mirror_y;
}

static error_t st7789_i8080_set_gap(Device* device, int32_t x_gap, int32_t y_gap) {
    auto* internal = static_cast<St7789I8080Internal*>(device_get_driver_data(device));
    return esp_lcd_panel_set_gap(internal->panel_handle, x_gap, y_gap) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t st7789_i8080_invert_color(Device* device, bool invert_color_data) {
    auto* internal = static_cast<St7789I8080Internal*>(device_get_driver_data(device));
    return esp_lcd_panel_invert_color(internal->panel_handle, invert_color_data) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t st7789_i8080_disp_on_off(Device* device, bool on_off) {
    auto* internal = static_cast<St7789I8080Internal*>(device_get_driver_data(device));
    return esp_lcd_panel_disp_on_off(internal->panel_handle, on_off) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t st7789_i8080_disp_sleep(Device* device, bool sleep) {
    auto* internal = static_cast<St7789I8080Internal*>(device_get_driver_data(device));
    return esp_lcd_panel_disp_sleep(internal->panel_handle, sleep) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

// swap_color_bytes=1 in the panel IO config means the i80 peripheral itself byte-swaps each
// RGB565 pixel over the parallel bus, matching how ili9341-module's SPI variant needs
// RGB565_SWAPPED - same reasoning, different transport.
static enum DisplayColorFormat st7789_i8080_get_color_format(Device*) {
    return DISPLAY_COLOR_FORMAT_RGB565_SWAPPED;
}

static uint16_t st7789_i8080_get_resolution_x(Device* device) {
    return GET_CONFIG(device)->horizontal_resolution;
}

static uint16_t st7789_i8080_get_resolution_y(Device* device) {
    return GET_CONFIG(device)->vertical_resolution;
}

static void st7789_i8080_get_frame_buffer(Device*, uint8_t, void** out_buffer) {
    *out_buffer = nullptr;
}

static uint8_t st7789_i8080_get_frame_buffer_count(Device*) {
    return 0;
}

static error_t st7789_i8080_get_backlight(Device* device, Device** backlight) {
    auto* configured_backlight = GET_CONFIG(device)->backlight;
    if (configured_backlight == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    *backlight = configured_backlight;
    return ERROR_NONE;
}

// endregion

static const DisplayApi st7789_i8080_display_api = {
    .reset = st7789_i8080_reset,
    .init = st7789_i8080_init,
    .draw_bitmap = st7789_i8080_draw_bitmap,
    .mirror = st7789_i8080_mirror,
    .swap_xy = st7789_i8080_swap_xy,
    .get_swap_xy = st7789_i8080_get_swap_xy,
    .get_mirror_x = st7789_i8080_get_mirror_x,
    .get_mirror_y = st7789_i8080_get_mirror_y,
    .set_gap = st7789_i8080_set_gap,
    .invert_color = st7789_i8080_invert_color,
    .disp_on_off = st7789_i8080_disp_on_off,
    .disp_sleep = st7789_i8080_disp_sleep,
    .get_color_format = st7789_i8080_get_color_format,
    .get_resolution_x = st7789_i8080_get_resolution_x,
    .get_resolution_y = st7789_i8080_get_resolution_y,
    .get_frame_buffer = st7789_i8080_get_frame_buffer,
    .get_frame_buffer_count = st7789_i8080_get_frame_buffer_count,
    .get_backlight = st7789_i8080_get_backlight,
};

Driver st7789_i8080_driver = {
    .name = "st7789_i8080",
    .compatible = (const char*[]) { "sitronix,st7789-i8080", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &st7789_i8080_display_api,
    .device_type = &DISPLAY_TYPE,
    .owner = &st7789_i8080_module,
    .internal = nullptr
};
