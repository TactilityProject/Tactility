// SPDX-License-Identifier: Apache-2.0
#include <drivers/ili9341.h>
#include <ili9341_module.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/esp32_spi.h>
#include <tactility/drivers/spi_controller.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <esp_err.h>
#include <esp_lcd_io_spi.h>
#include <esp_lcd_panel_commands.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_ili9341.h>

#include <freertos/semphr.h>

#include <cstdlib>

#define TAG "ILI9341"

#define GET_CONFIG(device) (static_cast<const Ili9341Config*>((device)->config))

// Maps gamma-curve devicetree index [0,3] to the MIPI DCS GAMSET (0x26) parameter value. Mirrors
// the deprecated HAL's EspLcdSpiDisplay::setGammaCurve() (Drivers/EspLcdCompat) - note the
// non-linear mapping, not index+1.
static const uint8_t GAMMA_CURVE_VALUES[4] = { 0x01, 0x04, 0x02, 0x08 };

struct Ili9341Internal {
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_handle_t panel_handle;
    // Given from ISR context by on_color_trans_done() once a queued SPI transfer physically
    // completes. draw_bitmap() blocks on this so it can honor DisplayApi's synchronous contract
    // (see lvgl_display.c: the caller reuses/overwrites the color buffer as soon as draw_bitmap
    // returns) - esp_lcd_panel_draw_bitmap() itself only queues the transfer and returns early.
    SemaphoreHandle_t draw_done_semaphore;
};

// Fires for every completed SPI transaction on this IO (not just draw_bitmap's color transfers -
// bring-up commands like reset/init/gap go through the same IO), called from ISR context.
static bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void* user_ctx) {
    auto* internal = static_cast<Ili9341Internal*>(user_ctx);
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
    check(device_get_type(parent) == &SPI_CONTROLLER_TYPE);

    const auto* spi_config = static_cast<const Esp32SpiConfig*>(parent->config);
    const auto* config = GET_CONFIG(device);

    struct GpioPinSpec cs_pin;
    if (esp32_spi_get_cs_pin(device, &cs_pin) != ERROR_NONE) {
        LOG_E(TAG, "Failed to resolve CS pin");
        return ERROR_RESOURCE;
    }

    auto* internal = static_cast<Ili9341Internal*>(malloc(sizeof(Ili9341Internal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    internal->draw_done_semaphore = xSemaphoreCreateBinary();
    if (internal->draw_done_semaphore == nullptr) {
        free(internal);
        return ERROR_OUT_OF_MEMORY;
    }

    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = pin_or_unused(cs_pin),
        .dc_gpio_num = pin_or_unused(config->pin_dc),
        .spi_mode = 0,
        .pclk_hz = config->pixel_clock_hz,
        .trans_queue_depth = config->transaction_queue_depth,
        .on_color_trans_done = on_color_trans_done,
        .user_ctx = internal,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .flags = {
            .dc_high_on_cmd = 0,
            .dc_low_on_data = 0,
            .dc_low_on_param = 0,
            .octal_mode = 0,
            .quad_mode = 0,
            .sio_mode = 1,
            .lsb_first = 0,
            .cs_high_active = 0,
        },
    };

    esp_err_t ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)spi_config->host, &io_config, &internal->io_handle);
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
        .bits_per_pixel = config->bits_per_pixel,
        .flags = { .reset_active_high = config->reset_active_high },
        .vendor_config = nullptr,
    };

    ret = esp_lcd_new_panel_ili9341(internal->io_handle, &panel_config, &internal->panel_handle);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to create panel: %s", esp_err_to_name(ret));
        esp_lcd_panel_io_del(internal->io_handle);
        vSemaphoreDelete(internal->draw_done_semaphore);
        free(internal);
        return ERROR_RESOURCE;
    }

    // Bring-up sequence, order matches EspLcdDisplayV2::applyConfiguration (proven correct on real ILI9341 panels).
    // Every failure path below must clean up fully: unlike stop_device, this is never retried by the kernel
    // if start_device fails (see device_start() in TactilityKernel), so a partial failure here would leak.
    bool ok =
        esp_lcd_panel_reset(internal->panel_handle) == ESP_OK &&
        esp_lcd_panel_init(internal->panel_handle) == ESP_OK &&
        (!config->invert_color || esp_lcd_panel_invert_color(internal->panel_handle, true) == ESP_OK);

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
    auto* internal = static_cast<Ili9341Internal*>(device_get_driver_data(device));

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

    vSemaphoreDelete(internal->draw_done_semaphore);
    free(internal);
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

// endregion

// region DisplayApi

static error_t ili9341_reset(Device* device) {
    auto* internal = static_cast<Ili9341Internal*>(device_get_driver_data(device));
    return esp_lcd_panel_reset(internal->panel_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t ili9341_init(Device* device) {
    auto* internal = static_cast<Ili9341Internal*>(device_get_driver_data(device));
    return esp_lcd_panel_init(internal->panel_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t ili9341_draw_bitmap(Device* device, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data) {
    auto* internal = static_cast<Ili9341Internal*>(device_get_driver_data(device));

    // Drain any stale signal left over from a prior non-draw transaction (bring-up commands like
    // reset/gap also complete through on_color_trans_done), so the take() below can only be
    // satisfied by this draw's own transfer completing.
    xSemaphoreTake(internal->draw_done_semaphore, 0);

    if (esp_lcd_panel_draw_bitmap(internal->panel_handle, x_start, y_start, x_end, y_end, color_data) != ESP_OK) {
        return ERROR_RESOURCE;
    }

    // Block until the SPI transfer physically completes: DisplayApi's draw_bitmap is a synchronous
    // contract (see lvgl_display.c), so the caller must be able to safely reuse/overwrite
    // color_data as soon as this call returns. esp_lcd_panel_draw_bitmap() only queues the
    // transfer and returns once it's handed to the SPI peripheral, not once it's finished.
    xSemaphoreTake(internal->draw_done_semaphore, portMAX_DELAY);
    return ERROR_NONE;
}

static error_t ili9341_mirror(Device* device, bool x_axis, bool y_axis) {
    auto* internal = static_cast<Ili9341Internal*>(device_get_driver_data(device));
    return esp_lcd_panel_mirror(internal->panel_handle, x_axis, y_axis) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t ili9341_swap_xy(Device* device, bool swap_axes) {
    auto* internal = static_cast<Ili9341Internal*>(device_get_driver_data(device));
    return esp_lcd_panel_swap_xy(internal->panel_handle, swap_axes) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

// Reads the devicetree-configured baseline, not live hardware state: swap_xy()/mirror() calls made after
// start_device() (e.g. by an LVGL rotation binding) intentionally don't change what "rotation 0" means here.
static bool ili9341_get_swap_xy(Device* device) {
    return GET_CONFIG(device)->swap_xy;
}

static bool ili9341_get_mirror_x(Device* device) {
    return GET_CONFIG(device)->mirror_x;
}

static bool ili9341_get_mirror_y(Device* device) {
    return GET_CONFIG(device)->mirror_y;
}

static error_t ili9341_set_gap(Device* device, int32_t x_gap, int32_t y_gap) {
    auto* internal = static_cast<Ili9341Internal*>(device_get_driver_data(device));
    return esp_lcd_panel_set_gap(internal->panel_handle, x_gap, y_gap) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t ili9341_invert_color(Device* device, bool invert_color_data) {
    auto* internal = static_cast<Ili9341Internal*>(device_get_driver_data(device));
    return esp_lcd_panel_invert_color(internal->panel_handle, invert_color_data) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t ili9341_disp_on_off(Device* device, bool on_off) {
    auto* internal = static_cast<Ili9341Internal*>(device_get_driver_data(device));
    return esp_lcd_panel_disp_on_off(internal->panel_handle, on_off) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t ili9341_disp_sleep(Device* device, bool sleep) {
    auto* internal = static_cast<Ili9341Internal*>(device_get_driver_data(device));
    return esp_lcd_panel_disp_sleep(internal->panel_handle, sleep) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

// bgr_order only selects the panel controller's rgb_ele_order (applied in start(), below) so the
// R/B swap happens in hardware over SPI. LVGL always fills RGB565 buffers either way - it has no
// BGR565 format (lvgl_display.c rejects it, "no LVGL equivalent"), and none is needed here.
//
// _SWAPPED (not plain RGB565): the panel expects each 16-bit pixel high-byte-first over SPI, but
// this CPU is little-endian, so a plain RGB565 buffer arrives byte-swapped per pixel - confirmed
// on real hardware: black/white rendered correctly (0x0000/0xFFFF are swap-invariant) while every
// other color was wrong. The old deprecated-HAL driver had the same requirement (its equivalent
// knob was esp_lvgl_port's `swapBytes = true` in Devices/*/Source/devices/Display.cpp).
static enum DisplayColorFormat ili9341_get_color_format(Device*) {
    return DISPLAY_COLOR_FORMAT_RGB565_SWAPPED;
}

static uint16_t ili9341_get_resolution_x(Device* device) {
    return GET_CONFIG(device)->horizontal_resolution;
}

static uint16_t ili9341_get_resolution_y(Device* device) {
    return GET_CONFIG(device)->vertical_resolution;
}

static void ili9341_get_frame_buffer(Device*, uint8_t, void** out_buffer) {
    *out_buffer = nullptr;
}

static uint8_t ili9341_get_frame_buffer_count(Device*) {
    return 0;
}

static error_t ili9341_get_backlight(Device* device, Device** backlight) {
    auto* configured_backlight = GET_CONFIG(device)->backlight;
    if (configured_backlight == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    *backlight = configured_backlight;
    return ERROR_NONE;
}

// endregion

static const DisplayApi ili9341_display_api = {
    .capabilities = DISPLAY_CAPABILITY_CAP_MIRROR | DISPLAY_CAPABILITY_CAP_SWAP_XY |
        DISPLAY_CAPABILITY_CAP_SET_GAP | DISPLAY_CAPABILITY_INVERT_COLOR | DISPLAY_CAPABILITY_ON_OFF |
        DISPLAY_CAPABILITY_SLEEP | DISPLAY_CAPABILITY_BACKLIGHT,
    .reset = ili9341_reset,
    .init = ili9341_init,
    .draw_bitmap = ili9341_draw_bitmap,
    .mirror = ili9341_mirror,
    .swap_xy = ili9341_swap_xy,
    .get_swap_xy = ili9341_get_swap_xy,
    .get_mirror_x = ili9341_get_mirror_x,
    .get_mirror_y = ili9341_get_mirror_y,
    .set_gap = ili9341_set_gap,
    .invert_color = ili9341_invert_color,
    .disp_on_off = ili9341_disp_on_off,
    .disp_sleep = ili9341_disp_sleep,
    .get_color_format = ili9341_get_color_format,
    .get_resolution_x = ili9341_get_resolution_x,
    .get_resolution_y = ili9341_get_resolution_y,
    .get_frame_buffer = ili9341_get_frame_buffer,
    .get_frame_buffer_count = ili9341_get_frame_buffer_count,
    .get_backlight = ili9341_get_backlight,
    .has_capability = nullptr,
};

Driver ili9341_driver = {
    .name = "ili9341",
    .compatible = (const char*[]) { "ilitek,ili9341", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &ili9341_display_api,
    .device_type = &DISPLAY_TYPE,
    .owner = &ili9341_module,
    .internal = nullptr
};
