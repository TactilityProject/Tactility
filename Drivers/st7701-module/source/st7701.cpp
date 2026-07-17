// SPDX-License-Identifier: Apache-2.0
#include <soc/soc_caps.h>
#if SOC_LCD_RGB_SUPPORTED

#include <drivers/st7701.h>
#include <st7701_module.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/display.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <esp_err.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_io_additions.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_lcd_st7701.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdlib>

#define TAG "St7701"
#define GET_CONFIG(device) (static_cast<const St7701Config*>((device)->config))

// Generic lvgl-module display glue (Modules/lvgl-module/source/lvgl_display.c) only ever asks
// for frame buffer index 0 and 1, so caching more than that would be dead weight.
constexpr size_t MAX_CACHED_FRAME_BUFFERS = 2;

struct St7701Internal {
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_handle_t panel_handle;
    void* frame_buffers[MAX_CACHED_FRAME_BUFFERS];
    uint8_t frame_buffer_count;
    // Size of each buffer in frame_buffers, in bytes - used to range-check whether a given
    // draw_bitmap() color_data pointer is actually one of them (see draw_bitmap() below).
    size_t frame_buffer_size_bytes;
    // Signaled by on_frame_buf_complete once per real DMA scan-out of a whole frame. Only
    // waited on in draw_bitmap() when color_data is one of frame_buffers - see the comment there.
    SemaphoreHandle_t frame_complete_semaphore;
    // Heap-allocated only when the devicetree supplies a custom init_sequence (see
    // parse_init_sequence()) - nullptr otherwise, since the vendor's built-in default sequence
    // needs no parsing. Its .data pointers alias directly into the devicetree's static const
    // byte buffer, so only this struct array itself needs freeing in stop().
    st7701_lcd_init_cmd_t* parsed_init_cmds;
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
    auto* internal = static_cast<St7701Internal*>(user_ctx);
    BaseType_t high_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(internal->frame_complete_semaphore, &high_task_woken);
    return high_task_woken == pdTRUE;
}

static int pin_or_unused(const GpioPinSpec& pin) {
    return pin.gpio_controller == nullptr ? -1 : static_cast<int>(pin.pin);
}

// Unpacks the devicetree's flat [cmd, data_len, delay_ms, data_len bytes...] encoding (produced
// by the devicetree compiler's "array" property type - see init-sequence in
// bindings/sitronix,st7701.yaml) into a heap-allocated st7701_lcd_init_cmd_t array. Each entry's
// .data points directly into `bytes`, which is the devicetree's static const buffer and outlives
// the device, so no per-entry copy is needed.
static bool parse_init_sequence(const uint8_t* bytes, uint32_t length, st7701_lcd_init_cmd_t** out_cmds, uint16_t* out_count) {
    uint32_t count = 0;
    for (uint32_t offset = 0; offset < length; count++) {
        if (offset + 3 > length) {
            LOG_E(TAG, "init-sequence truncated: entry header runs past the end of the array");
            return false;
        }
        offset += 3 + bytes[offset + 1];
        if (offset > length) {
            LOG_E(TAG, "init-sequence truncated: entry data runs past the end of the array");
            return false;
        }
    }

    auto* cmds = static_cast<st7701_lcd_init_cmd_t*>(malloc(count * sizeof(st7701_lcd_init_cmd_t)));
    if (cmds == nullptr) {
        return false;
    }

    uint32_t offset = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint8_t data_len = bytes[offset + 1];
        cmds[i] = {
            .cmd = bytes[offset],
            .data = data_len > 0 ? &bytes[offset + 3] : nullptr,
            .data_bytes = data_len,
            .delay_ms = bytes[offset + 2],
        };
        offset += 3 + data_len;
    }

    *out_cmds = cmds;
    *out_count = (uint16_t)count;
    return true;
}

// region Driver lifecycle

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);

    auto* internal = static_cast<St7701Internal*>(malloc(sizeof(St7701Internal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    internal->parsed_init_cmds = nullptr;

    const st7701_lcd_init_cmd_t* init_cmds = nullptr;
    uint16_t init_cmds_size = 0;
    if (config->init_sequence != nullptr && config->init_sequence_length > 0) {
        if (!parse_init_sequence(config->init_sequence, config->init_sequence_length, &internal->parsed_init_cmds, &init_cmds_size)) {
            LOG_E(TAG, "Failed to parse init-sequence property");
            free(internal);
            return ERROR_INVALID_ARGUMENT;
        }
        init_cmds = internal->parsed_init_cmds;
    }

    spi_line_config_t line_config = {
        .cs_io_type = IO_TYPE_GPIO,
        .cs_gpio_num = pin_or_unused(config->pin_cs),
        .scl_io_type = IO_TYPE_GPIO,
        .scl_gpio_num = pin_or_unused(config->pin_scl),
        .sda_io_type = IO_TYPE_GPIO,
        .sda_gpio_num = pin_or_unused(config->pin_sda),
        .io_expander = nullptr,
    };
    // scl_active_edge is a runtime bool, not a compile-time constant, so it can't be passed
    // through the macro's brace-init `.spi_mode = scl_active_edge ? 1 : 0` directly - that trips
    // -Werror=narrowing on the bitfield (only literal 0/1 args avoid it). Assign it afterward
    // instead, as a plain (non-narrowing) assignment.
    esp_lcd_panel_io_3wire_spi_config_t io_config = ST7701_PANEL_IO_3WIRE_SPI_CONFIG(line_config, 0);
    io_config.spi_mode = config->scl_active_edge ? 1 : 0;

    esp_err_t ret = esp_lcd_new_panel_io_3wire_spi(&io_config, &internal->io_handle);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to create panel IO: %s", esp_err_to_name(ret));
        free(internal->parsed_init_cmds);
        free(internal);
        return ERROR_RESOURCE;
    }

    esp_lcd_rgb_panel_config_t rgb_config = {
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
    // more data lines than that, the tail of the array must be explicitly marked unused rather
    // than left as the aggregate-init default of 0 (which would look like "GPIO0 is wired here").
    for (size_t i = 16; i < sizeof(rgb_config.data_gpio_nums) / sizeof(rgb_config.data_gpio_nums[0]); i++) {
        rgb_config.data_gpio_nums[i] = -1;
    }

    st7701_vendor_config_t vendor_config = {
        .init_cmds = init_cmds,
        .init_cmds_size = init_cmds_size,
        .rgb_config = &rgb_config,
        .flags = {
            .use_mipi_interface = 0,
            .mirror_by_cmd = config->mirror_by_cmd,
            .auto_del_panel_io = config->auto_del_panel_io,
        },
    };

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = pin_or_unused(config->pin_reset),
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = config->bits_per_pixel,
        .flags = { .reset_active_high = config->reset_active_high },
        .vendor_config = &vendor_config,
    };

    ret = esp_lcd_new_panel_st7701(internal->io_handle, &panel_config, &internal->panel_handle);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to create panel: %s", esp_err_to_name(ret));
        esp_lcd_panel_io_del(internal->io_handle);
        free(internal->parsed_init_cmds);
        free(internal);
        return ERROR_RESOURCE;
    }

    // Bring-up sequence: reset() pulses (or software-resets) the panel and init() pushes
    // init_cmds over the 3-wire bus before bringing up the underlying RGB peripheral. Every
    // failure path below must clean up fully: unlike stop_device, this is never retried by the
    // kernel if start_device fails (see device_start() in TactilityKernel), so a partial failure
    // here would leak.
    bool ok =
        esp_lcd_panel_reset(internal->panel_handle) == ESP_OK &&
        esp_lcd_panel_init(internal->panel_handle) == ESP_OK &&
        esp_lcd_panel_swap_xy(internal->panel_handle, config->swap_xy) == ESP_OK &&
        esp_lcd_panel_mirror(internal->panel_handle, config->mirror_x, config->mirror_y) == ESP_OK &&
        esp_lcd_panel_invert_color(internal->panel_handle, config->invert_color) == ESP_OK &&
        esp_lcd_panel_disp_on_off(internal->panel_handle, true) == ESP_OK;

    if (!ok) {
        LOG_E(TAG, "Failed to bring up panel");
        esp_lcd_panel_del(internal->panel_handle);
        if (!config->auto_del_panel_io) {
            esp_lcd_panel_io_del(internal->io_handle);
        }
        free(internal->parsed_init_cmds);
        free(internal);
        return ERROR_RESOURCE;
    }

    internal->frame_buffer_count = 0;
    internal->frame_buffer_size_bytes = (size_t)config->horizontal_resolution * config->vertical_resolution *
        ((config->bits_per_pixel + 7) / 8);
    if (config->num_fbs > 0) {
        // esp_lcd_rgb_panel_get_frame_buffer() is variadic: the number of out-pointer arguments
        // passed must match fb_num exactly, so this can't be a loop.
        size_t fb_num = config->num_fbs < MAX_CACHED_FRAME_BUFFERS ? config->num_fbs : MAX_CACHED_FRAME_BUFFERS;
        switch (fb_num) {
            case 1:
                ret = esp_lcd_rgb_panel_get_frame_buffer(internal->panel_handle, 1, &internal->frame_buffers[0]);
                break;
            case 2:
                ret = esp_lcd_rgb_panel_get_frame_buffer(internal->panel_handle, 2, &internal->frame_buffers[0], &internal->frame_buffers[1]);
                break;
            default:
                ret = ESP_OK;
                break;
        }

        if (ret != ESP_OK) {
            LOG_E(TAG, "Failed to get frame buffer(s): %s", esp_err_to_name(ret));
            esp_lcd_panel_del(internal->panel_handle);
            if (!config->auto_del_panel_io) {
                esp_lcd_panel_io_del(internal->io_handle);
            }
            free(internal->parsed_init_cmds);
            free(internal);
            return ERROR_RESOURCE;
        }
        internal->frame_buffer_count = (uint8_t)fb_num;
    }

    internal->frame_complete_semaphore = xSemaphoreCreateBinary();
    if (internal->frame_complete_semaphore == nullptr) {
        esp_lcd_panel_del(internal->panel_handle);
        if (!config->auto_del_panel_io) {
            esp_lcd_panel_io_del(internal->io_handle);
        }
        free(internal->parsed_init_cmds);
        free(internal);
        return ERROR_OUT_OF_MEMORY;
    }

    esp_lcd_rgb_panel_event_callbacks_t callbacks = {};
    callbacks.on_frame_buf_complete = on_frame_buf_complete;
    if (esp_lcd_rgb_panel_register_event_callbacks(internal->panel_handle, &callbacks, internal) != ESP_OK) {
        LOG_E(TAG, "Failed to register panel event callbacks");
        vSemaphoreDelete(internal->frame_complete_semaphore);
        esp_lcd_panel_del(internal->panel_handle);
        if (!config->auto_del_panel_io) {
            esp_lcd_panel_io_del(internal->io_handle);
        }
        free(internal->parsed_init_cmds);
        free(internal);
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    const auto* config = GET_CONFIG(device);
    auto* internal = static_cast<St7701Internal*>(device_get_driver_data(device));

    if (internal->panel_handle != nullptr) {
        if (esp_lcd_panel_del(internal->panel_handle) != ESP_OK) {
            LOG_E(TAG, "Failed to delete panel");
            return ERROR_RESOURCE;
        }
        internal->panel_handle = nullptr;
    }

    // If auto_del_panel_io was set, esp_lcd_new_panel_st7701() already deleted the IO handle
    // itself right after bring-up (to free pins shared with the RGB bus) - deleting it again
    // here would double-free.
    if (!config->auto_del_panel_io && internal->io_handle != nullptr) {
        if (esp_lcd_panel_io_del(internal->io_handle) != ESP_OK) {
            LOG_E(TAG, "Failed to delete panel IO");
            return ERROR_RESOURCE;
        }
        internal->io_handle = nullptr;
    }

    vSemaphoreDelete(internal->frame_complete_semaphore);
    free(internal->parsed_init_cmds);
    free(internal);
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

// endregion

// region DisplayApi

static error_t st7701_reset(Device* device) {
    auto* internal = static_cast<St7701Internal*>(device_get_driver_data(device));
    return esp_lcd_panel_reset(internal->panel_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t st7701_init(Device* device) {
    auto* internal = static_cast<St7701Internal*>(device_get_driver_data(device));
    return esp_lcd_panel_init(internal->panel_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

// Only block for scan-out completion when color_data is actually one of the panel's own frame
// buffers (see on_frame_buf_complete's comment above for why that matters) - i.e. this specific
// call is a zero-copy flip, not a plain CPU copy into the panel's buffer from a caller-owned one
// (e.g. LVGL bound in owned-buffer mode), which has no reuse race to guard against and shouldn't
// pay the up-to-one-frame latency cost for every partial update.
static bool st7701_color_data_is_frame_buffer(const St7701Internal* internal, const void* color_data) {
    const auto* ptr = static_cast<const uint8_t*>(color_data);
    for (uint8_t i = 0; i < internal->frame_buffer_count; i++) {
        const auto* base = static_cast<const uint8_t*>(internal->frame_buffers[i]);
        if (ptr >= base && ptr < base + internal->frame_buffer_size_bytes) {
            return true;
        }
    }
    return false;
}

static error_t st7701_draw_bitmap(Device* device, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data) {
    auto* internal = static_cast<St7701Internal*>(device_get_driver_data(device));

    bool wait_for_scanout = st7701_color_data_is_frame_buffer(internal, color_data);
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

static error_t st7701_mirror(Device* device, bool x_axis, bool y_axis) {
    auto* internal = static_cast<St7701Internal*>(device_get_driver_data(device));
    return esp_lcd_panel_mirror(internal->panel_handle, x_axis, y_axis) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t st7701_swap_xy(Device* device, bool swap_axes) {
    auto* internal = static_cast<St7701Internal*>(device_get_driver_data(device));
    return esp_lcd_panel_swap_xy(internal->panel_handle, swap_axes) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static bool st7701_get_swap_xy(Device* device) {
    return GET_CONFIG(device)->swap_xy;
}

static bool st7701_get_mirror_x(Device* device) {
    return GET_CONFIG(device)->mirror_x;
}

static bool st7701_get_mirror_y(Device* device) {
    return GET_CONFIG(device)->mirror_y;
}

// set_gap is not exposed: like plain RGB panels, the ST7701's RGB interface is a raw scan-out
// framebuffer with no addressable-window concept, so there's no gap to set.

static error_t st7701_invert_color(Device* device, bool invert_color_data) {
    auto* internal = static_cast<St7701Internal*>(device_get_driver_data(device));
    return esp_lcd_panel_invert_color(internal->panel_handle, invert_color_data) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t st7701_disp_on_off(Device* device, bool on_off) {
    auto* internal = static_cast<St7701Internal*>(device_get_driver_data(device));
    return esp_lcd_panel_disp_on_off(internal->panel_handle, on_off) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

// disp_sleep is not exposed: esp_lcd_st7701's RGB variant has no sleep-mode entry point of its
// own (only init/del/reset/mirror/disp_on_off are overridden - see esp_lcd_st7701_rgb.c).

static enum DisplayColorFormat st7701_get_color_format(Device*) {
    return DISPLAY_COLOR_FORMAT_RGB565;
}

static uint16_t st7701_get_resolution_x(Device* device) {
    return GET_CONFIG(device)->horizontal_resolution;
}

static uint16_t st7701_get_resolution_y(Device* device) {
    return GET_CONFIG(device)->vertical_resolution;
}

static void st7701_get_frame_buffer(Device* device, uint8_t index, void** out_buffer) {
    auto* internal = static_cast<St7701Internal*>(device_get_driver_data(device));
    *out_buffer = index < internal->frame_buffer_count ? internal->frame_buffers[index] : nullptr;
}

static uint8_t st7701_get_frame_buffer_count(Device* device) {
    auto* internal = static_cast<St7701Internal*>(device_get_driver_data(device));
    return internal->frame_buffer_count;
}

static error_t st7701_get_backlight(Device* device, Device** backlight) {
    auto* configured_backlight = GET_CONFIG(device)->backlight;
    if (configured_backlight == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    *backlight = configured_backlight;
    return ERROR_NONE;
}

constexpr uint32_t ST7701_CAPABILITIES = DISPLAY_CAPABILITY_CAP_MIRROR | DISPLAY_CAPABILITY_CAP_SWAP_XY |
    DISPLAY_CAPABILITY_INVERT_COLOR | DISPLAY_CAPABILITY_ON_OFF | DISPLAY_CAPABILITY_BACKLIGHT;

// Same reasoning as rgb_display's has_capability(): mirror()/swap_xy()'s rotate_mask is only
// applied when draw_bitmap()'s color_data is copied into the frame buffer by CPU. When
// frame_buffer_count > 0, LVGL is bound directly onto the panel's own frame buffers, so that copy
// - and with it the rotation - never happens for CAP_SWAP_XY. CAP_MIRROR is unaffected here
// because mirror_by_cmd (if set) still takes effect regardless of the frame-buffer-bound path.
static bool st7701_has_capability(Device* device, uint32_t capability) {
    auto* internal = static_cast<St7701Internal*>(device_get_driver_data(device));
    uint32_t capabilities = ST7701_CAPABILITIES;
    if (internal->frame_buffer_count > 0 && !GET_CONFIG(device)->mirror_by_cmd) {
        capabilities &= ~DISPLAY_CAPABILITY_CAP_MIRROR;
    }
    if (internal->frame_buffer_count > 0) {
        capabilities &= ~DISPLAY_CAPABILITY_CAP_SWAP_XY;
    }
    return (capabilities & capability) == capability;
}

// endregion

static const DisplayApi st7701_display_api = {
    .capabilities = ST7701_CAPABILITIES,
    .reset = st7701_reset,
    .init = st7701_init,
    .draw_bitmap = st7701_draw_bitmap,
    .mirror = st7701_mirror,
    .swap_xy = st7701_swap_xy,
    .get_swap_xy = st7701_get_swap_xy,
    .get_mirror_x = st7701_get_mirror_x,
    .get_mirror_y = st7701_get_mirror_y,
    .set_gap = nullptr,
    .invert_color = st7701_invert_color,
    .disp_on_off = st7701_disp_on_off,
    .disp_sleep = nullptr,
    .get_color_format = st7701_get_color_format,
    .get_resolution_x = st7701_get_resolution_x,
    .get_resolution_y = st7701_get_resolution_y,
    .get_frame_buffer = st7701_get_frame_buffer,
    .get_frame_buffer_count = st7701_get_frame_buffer_count,
    .get_backlight = st7701_get_backlight,
    .has_capability = st7701_has_capability,
};

Driver st7701_driver = {
    .name = "st7701",
    .compatible = (const char*[]) { "sitronix,st7701", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &st7701_display_api,
    .device_type = &DISPLAY_TYPE,
    .owner = &st7701_module,
    .internal = nullptr
};

#endif // SOC_LCD_RGB_SUPPORTED
