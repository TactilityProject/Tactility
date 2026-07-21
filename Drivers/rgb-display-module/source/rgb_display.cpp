// SPDX-License-Identifier: Apache-2.0
#include <soc/soc_caps.h>
#if SOC_LCD_RGB_SUPPORTED

#include <drivers/rgb_display.h>
#include <rgb_display_module.h>

#include <tactility/delay.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/gpio.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <esp_err.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_lcd_panel_ops.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdlib>

#define TAG "RgbDisplay"
#define GET_CONFIG(device) (static_cast<const RgbDisplayConfig*>((device)->config))

// Generic lvgl-module display glue (Modules/lvgl-module/source/lvgl_display.c) only ever asks
// for frame buffer index 0 and 1, so caching more than that would be dead weight.
constexpr size_t MAX_CACHED_FRAME_BUFFERS = 2;

struct RgbDisplayInternal {
    esp_lcd_panel_handle_t panel_handle;
    void* frame_buffers[MAX_CACHED_FRAME_BUFFERS];
    uint8_t frame_buffer_count;
    // Size of each buffer in frame_buffers, in bytes - used to range-check whether a given
    // draw_bitmap() color_data pointer is actually one of them (see rgb_display_draw_bitmap()).
    size_t frame_buffer_size_bytes;
    // Signaled by on_frame_buf_complete once per real DMA scan-out of a whole frame. Only
    // waited on in draw_bitmap() when color_data is one of frame_buffers - see the comment there for why.
    SemaphoreHandle_t frame_complete_semaphore;
};

// esp_lcd_rgb_panel's draw_bitmap() has a zero-copy path when color_data is one of the panel's
// own frame buffers (as returned by esp_lcd_rgb_panel_get_frame_buffer()): it just repoints which
// buffer is scanned out and returns almost instantly - well before the RGB peripheral's DMA has
// actually finished scanning out the *previous* buffer, let alone started on this one. Callers in
// full/direct LVGL render mode render straight into these real frame buffers, so if draw_bitmap()
// returned that quickly, LVGL would be free to start overwriting the *other* buffer - which may
// still be mid-scanout - producing visible tearing/flashing. on_frame_buf_complete fires once per
// actual whole-frame DMA completion (continuously, at the panel's refresh rate, independent of
// draw_bitmap calls), so waiting for the next occurrence after each draw_bitmap() genuinely
// blocks until it's safe to start writing into the frame buffers again.
static bool IRAM_ATTR on_frame_buf_complete(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t*, void* user_ctx) {
    auto* internal = static_cast<RgbDisplayInternal*>(user_ctx);
    BaseType_t high_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(internal->frame_complete_semaphore, &high_task_woken);
    return high_task_woken == pdTRUE;
}

static int pin_or_unused(const GpioPinSpec& pin) {
    return pin.gpio_controller == nullptr ? -1 : static_cast<int>(pin.pin);
}

// Pulses the panel's own driver-IC reset line, if configured. Transient: the descriptor is
// released immediately after, since nothing else needs to touch this pin afterward.
static error_t perform_hardware_reset(const RgbDisplayConfig* config) {
    if (config->pin_reset.gpio_controller == nullptr) {
        return ERROR_NONE;
    }

    auto* descriptor = gpio_descriptor_acquire(config->pin_reset.gpio_controller, config->pin_reset.pin, GPIO_FLAG_DIRECTION_OUTPUT | GPIO_FLAG_ACTIVE_LOW, GPIO_OWNER_GPIO);
    if (descriptor == nullptr) {
        LOG_E(TAG, "Failed to acquire reset GPIO descriptor");
        return ERROR_RESOURCE;
    }

    bool ok = gpio_descriptor_set_level(descriptor, true) == ERROR_NONE;
    if (ok) {
        delay_millis(100);
        ok = gpio_descriptor_set_level(descriptor, false) == ERROR_NONE;
        delay_millis(10);
    }

    gpio_descriptor_release(descriptor);
    return ok ? ERROR_NONE : ERROR_RESOURCE;
}

// region Driver lifecycle

static error_t cache_frame_buffers(RgbDisplayInternal* internal, const RgbDisplayConfig* config) {
    internal->frame_buffer_count = 0;
    internal->frame_buffer_size_bytes = (size_t)config->horizontal_resolution * config->vertical_resolution *
        ((config->bits_per_pixel + 7) / 8);
    if (config->num_fbs == 0) {
        return ERROR_NONE;
    }

    // esp_lcd_rgb_panel_get_frame_buffer() is variadic: the number of out-pointer arguments
    // passed must match fb_num exactly, so this can't be a loop.
    size_t fb_num = config->num_fbs < MAX_CACHED_FRAME_BUFFERS ? config->num_fbs : MAX_CACHED_FRAME_BUFFERS;
    esp_err_t ret;
    switch (fb_num) {
        case 1:
            ret = esp_lcd_rgb_panel_get_frame_buffer(internal->panel_handle, 1, &internal->frame_buffers[0]);
            break;
        case 2:
            ret = esp_lcd_rgb_panel_get_frame_buffer(internal->panel_handle, 2, &internal->frame_buffers[0], &internal->frame_buffers[1]);
            break;
        default:
            return ERROR_NONE;
    }

    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to get frame buffer(s): %s", esp_err_to_name(ret));
        return ERROR_RESOURCE;
    }

    internal->frame_buffer_count = (uint8_t)fb_num;
    return ERROR_NONE;
}

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);

    auto* internal = static_cast<RgbDisplayInternal*>(malloc(sizeof(RgbDisplayInternal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    error_t reset_error = perform_hardware_reset(config);
    if (reset_error != ERROR_NONE) {
        LOG_E(TAG, "Failed to reset panel");
        free(internal);
        return reset_error;
    }

    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = config->pixel_clock_hz,
            .h_res = config->horizontal_resolution,
            .v_res = config->vertical_resolution,
            .hsync_pulse_width = config->hsync_pulse_width,
            .hsync_back_porch = config->hsync_back_porch,
            .hsync_front_porch = config->hsync_front_porch,
            .vsync_pulse_width = config->vsync_pulse_width,
            .vsync_back_porch = config->vsync_back_porch,
            .vsync_front_porch = config->vsync_front_porch,
            .flags = {
                .hsync_idle_low = config->hsync_idle_low,
                .vsync_idle_low = config->vsync_idle_low,
                .de_idle_high = config->de_idle_high,
                .pclk_active_neg = config->pclk_active_neg,
                .pclk_idle_high = config->pclk_idle_high,
            }
        },
        .data_width = config->data_width,
        .bits_per_pixel = config->bits_per_pixel,
        .num_fbs = config->num_fbs,
        .bounce_buffer_size_px = config->bounce_buffer_size_px,
        .sram_trans_align = config->sram_trans_align,
        .psram_trans_align = config->psram_trans_align,
        .hsync_gpio_num = pin_or_unused(config->pin_hsync),
        .vsync_gpio_num = pin_or_unused(config->pin_vsync),
        .de_gpio_num = pin_or_unused(config->pin_de),
        .pclk_gpio_num = pin_or_unused(config->pin_pclk),
        .disp_gpio_num = pin_or_unused(config->pin_disp),
        .data_gpio_nums = {
            pin_or_unused(config->pin_data0),
            pin_or_unused(config->pin_data1),
            pin_or_unused(config->pin_data2),
            pin_or_unused(config->pin_data3),
            pin_or_unused(config->pin_data4),
            pin_or_unused(config->pin_data5),
            pin_or_unused(config->pin_data6),
            pin_or_unused(config->pin_data7),
            pin_or_unused(config->pin_data8),
            pin_or_unused(config->pin_data9),
            pin_or_unused(config->pin_data10),
            pin_or_unused(config->pin_data11),
            pin_or_unused(config->pin_data12),
            pin_or_unused(config->pin_data13),
            pin_or_unused(config->pin_data14),
            pin_or_unused(config->pin_data15),
        },
        .flags = {
            .disp_active_low = config->disp_active_low,
            .refresh_on_demand = config->refresh_on_demand,
            .fb_in_psram = config->fb_in_psram,
            .double_fb = config->double_fb,
            .no_fb = config->no_fb,
            .bb_invalidate_cache = config->bb_invalidate_cache,
        }
    };

    // This Config struct only exposes 16 named data pins, so on chips whose RGB peripheral has
    // more data lines than that (e.g. ESP32-P4's 24), the tail of the array must be explicitly
    // marked unused rather than left as the aggregate-init default of 0 (which would look like
    // "GPIO0 is wired to this line").
    for (size_t i = 16; i < sizeof(panel_config.data_gpio_nums) / sizeof(panel_config.data_gpio_nums[0]); i++) {
        panel_config.data_gpio_nums[i] = -1;
    }

    esp_err_t ret = esp_lcd_new_rgb_panel(&panel_config, &internal->panel_handle);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to create panel: %s", esp_err_to_name(ret));
        free(internal);
        return ERROR_RESOURCE;
    }

    bool ok =
        esp_lcd_panel_reset(internal->panel_handle) == ESP_OK &&
        esp_lcd_panel_init(internal->panel_handle) == ESP_OK &&
        esp_lcd_panel_swap_xy(internal->panel_handle, config->swap_xy) == ESP_OK &&
        esp_lcd_panel_mirror(internal->panel_handle, config->mirror_x, config->mirror_y) == ESP_OK &&
        esp_lcd_panel_invert_color(internal->panel_handle, config->invert_color) == ESP_OK;

    if (!ok) {
        LOG_E(TAG, "Failed to bring up panel");
        esp_lcd_panel_del(internal->panel_handle);
        free(internal);
        return ERROR_RESOURCE;
    }

    error_t error = cache_frame_buffers(internal, config);
    if (error != ERROR_NONE) {
        esp_lcd_panel_del(internal->panel_handle);
        free(internal);
        return error;
    }

    internal->frame_complete_semaphore = xSemaphoreCreateBinary();
    if (internal->frame_complete_semaphore == nullptr) {
        esp_lcd_panel_del(internal->panel_handle);
        free(internal);
        return ERROR_OUT_OF_MEMORY;
    }

    esp_lcd_rgb_panel_event_callbacks_t callbacks = {};
    callbacks.on_frame_buf_complete = on_frame_buf_complete;
    if (esp_lcd_rgb_panel_register_event_callbacks(internal->panel_handle, &callbacks, internal) != ESP_OK) {
        LOG_E(TAG, "Failed to register panel event callbacks");
        vSemaphoreDelete(internal->frame_complete_semaphore);
        esp_lcd_panel_del(internal->panel_handle);
        free(internal);
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<RgbDisplayInternal*>(device_get_driver_data(device));

    if (internal->panel_handle != nullptr) {
        if (esp_lcd_panel_del(internal->panel_handle) != ESP_OK) {
            LOG_E(TAG, "Failed to delete panel");
            return ERROR_RESOURCE;
        }
        internal->panel_handle = nullptr;
    }

    vSemaphoreDelete(internal->frame_complete_semaphore);
    free(internal);
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

// endregion

// region DisplayApi

static error_t rgb_display_reset(Device* device) {
    auto* internal = static_cast<RgbDisplayInternal*>(device_get_driver_data(device));
    return esp_lcd_panel_reset(internal->panel_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t rgb_display_init(Device* device) {
    auto* internal = static_cast<RgbDisplayInternal*>(device_get_driver_data(device));
    return esp_lcd_panel_init(internal->panel_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

// Only block for scan-out completion when color_data is actually one of the panel's own frame
// buffers (see on_frame_buf_complete's comment above for why that matters) - i.e. this specific
// call is a zero-copy flip, not a plain CPU copy into the panel's buffer from a caller-owned one
// (e.g. LVGL bound in owned-buffer mode), which has no reuse race to guard against and shouldn't
// pay the up-to-one-frame latency cost for every partial update.
static bool rgb_display_color_data_is_frame_buffer(const RgbDisplayInternal* internal, const void* color_data) {
    const auto* ptr = static_cast<const uint8_t*>(color_data);
    for (uint8_t i = 0; i < internal->frame_buffer_count; i++) {
        const auto* base = static_cast<const uint8_t*>(internal->frame_buffers[i]);
        if (ptr >= base && ptr < base + internal->frame_buffer_size_bytes) {
            return true;
        }
    }
    return false;
}

static error_t rgb_display_draw_bitmap(Device* device, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data) {
    auto* internal = static_cast<RgbDisplayInternal*>(device_get_driver_data(device));

    bool wait_for_scanout = rgb_display_color_data_is_frame_buffer(internal, color_data);
    if (wait_for_scanout) {
        xSemaphoreTake(internal->frame_complete_semaphore, 0); // clear any already-pending signal
    }

    if (esp_lcd_panel_draw_bitmap(internal->panel_handle, x_start, y_start, x_end, y_end, color_data) != ESP_OK) {
        return ERROR_RESOURCE;
    }

    if (wait_for_scanout) {
        xSemaphoreTake(internal->frame_complete_semaphore, portMAX_DELAY);
    }

    return ERROR_NONE;
}

static error_t rgb_display_mirror(Device* device, bool x_axis, bool y_axis) {
    auto* internal = static_cast<RgbDisplayInternal*>(device_get_driver_data(device));
    return esp_lcd_panel_mirror(internal->panel_handle, x_axis, y_axis) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t rgb_display_swap_xy(Device* device, bool swap_axes) {
    auto* internal = static_cast<RgbDisplayInternal*>(device_get_driver_data(device));
    return esp_lcd_panel_swap_xy(internal->panel_handle, swap_axes) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static bool rgb_display_get_swap_xy(Device* device) {
    return GET_CONFIG(device)->swap_xy;
}

static bool rgb_display_get_mirror_x(Device* device) {
    return GET_CONFIG(device)->mirror_x;
}

static bool rgb_display_get_mirror_y(Device* device) {
    return GET_CONFIG(device)->mirror_y;
}

// set_gap is not exposed: RGB panels are raw scan-out framebuffers with no addressable-window
// concept the way MIPI/SPI panels have, so there's no gap to set.

static error_t rgb_display_invert_color(Device* device, bool invert_color_data) {
    auto* internal = static_cast<RgbDisplayInternal*>(device_get_driver_data(device));
    return esp_lcd_panel_invert_color(internal->panel_handle, invert_color_data) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t rgb_display_disp_on_off(Device* device, bool on_off) {
    auto* internal = static_cast<RgbDisplayInternal*>(device_get_driver_data(device));
    return esp_lcd_panel_disp_on_off(internal->panel_handle, on_off) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

// disp_sleep is not exposed: RGB panels have no MIPI DCS command interface, so there's no sleep
// mode to enter.

static enum DisplayColorFormat rgb_display_get_color_format(Device*) {
    return DISPLAY_COLOR_FORMAT_RGB565;
}

static uint16_t rgb_display_get_resolution_x(Device* device) {
    return GET_CONFIG(device)->horizontal_resolution;
}

static uint16_t rgb_display_get_resolution_y(Device* device) {
    return GET_CONFIG(device)->vertical_resolution;
}

static void rgb_display_get_frame_buffer(Device* device, uint8_t index, void** out_buffer) {
    auto* internal = static_cast<RgbDisplayInternal*>(device_get_driver_data(device));
    *out_buffer = index < internal->frame_buffer_count ? internal->frame_buffers[index] : nullptr;
}

static uint8_t rgb_display_get_frame_buffer_count(Device* device) {
    auto* internal = static_cast<RgbDisplayInternal*>(device_get_driver_data(device));
    return internal->frame_buffer_count;
}

static error_t rgb_display_get_backlight(Device* device, Device** backlight) {
    auto* configured_backlight = GET_CONFIG(device)->backlight;
    if (configured_backlight == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    *backlight = configured_backlight;
    return ERROR_NONE;
}

constexpr uint32_t RGB_DISPLAY_CAPABILITIES = DISPLAY_CAPABILITY_CAP_MIRROR | DISPLAY_CAPABILITY_CAP_SWAP_XY |
    DISPLAY_CAPABILITY_INVERT_COLOR | DISPLAY_CAPABILITY_ON_OFF | DISPLAY_CAPABILITY_BACKLIGHT;

// esp_lcd_panel_rgb only applies mirror()/swap_xy()'s rotate_mask when draw_bitmap()'s color_data
// is copied into the frame buffer by CPU. When frame_buffer_count > 0, LVGL is bound directly onto
// the panel's own frame buffers (see lvgl_display.c), so color_data always already *is* the frame
// buffer and that copy - and with it the rotation - never happens. Report those two capabilities as
// unavailable in that configuration so callers (e.g. lvgl_display.c) don't rely on a rotation that
// silently does nothing.
static bool rgb_display_has_capability(Device* device, uint32_t capability) {
    auto* internal = static_cast<RgbDisplayInternal*>(device_get_driver_data(device));
    uint32_t capabilities = RGB_DISPLAY_CAPABILITIES;
    if (internal->frame_buffer_count > 0) {
        capabilities &= ~(DISPLAY_CAPABILITY_CAP_MIRROR | DISPLAY_CAPABILITY_CAP_SWAP_XY);
    }
    return (capabilities & capability) == capability;
}

// endregion

static const DisplayApi rgb_display_api = {
    .capabilities = RGB_DISPLAY_CAPABILITIES,
    .reset = rgb_display_reset,
    .init = rgb_display_init,
    .draw_bitmap = rgb_display_draw_bitmap,
    .mirror = rgb_display_mirror,
    .swap_xy = rgb_display_swap_xy,
    .get_swap_xy = rgb_display_get_swap_xy,
    .get_mirror_x = rgb_display_get_mirror_x,
    .get_mirror_y = rgb_display_get_mirror_y,
    .set_gap = nullptr,
    .invert_color = rgb_display_invert_color,
    .disp_on_off = rgb_display_disp_on_off,
    .disp_sleep = nullptr,
    .get_color_format = rgb_display_get_color_format,
    .get_resolution_x = rgb_display_get_resolution_x,
    .get_resolution_y = rgb_display_get_resolution_y,
    .get_frame_buffer = rgb_display_get_frame_buffer,
    .get_frame_buffer_count = rgb_display_get_frame_buffer_count,
    .get_backlight = rgb_display_get_backlight,
    .has_capability = rgb_display_has_capability,
};

Driver rgb_display_driver = {
    .name = "rgb_display",
    .compatible = (const char*[]) { "espressif,esp32-rgb-display", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &rgb_display_api,
    .device_type = &DISPLAY_TYPE,
    .owner = &rgb_display_module,
    .internal = nullptr
};

#endif // SOC_LCD_RGB_SUPPORTED
