// SPDX-License-Identifier: Apache-2.0
#include <soc/soc_caps.h>
#if SOC_MIPI_DSI_SUPPORTED

#include <drivers/jd9165.h>
#include <jd9165_module.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/display.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <esp_err.h>
#include <esp_ldo_regulator.h>
#include <esp_lcd_jd9165.h>
#include <esp_lcd_mipi_dsi.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdlib>

#define TAG "Jd9165"
#define GET_CONFIG(device) (static_cast<const Jd9165Config*>((device)->config))

// Generic lvgl-module display glue (Modules/lvgl-module/source/lvgl_display.c) only ever asks
// for frame buffer index 0 and 1, so caching more than that would be dead weight.
constexpr size_t MAX_CACHED_FRAME_BUFFERS = 2;

struct Jd9165Internal {
    esp_ldo_channel_handle_t ldo_handle;
    esp_lcd_dsi_bus_handle_t dsi_bus_handle;
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_handle_t panel_handle;
    void* frame_buffers[MAX_CACHED_FRAME_BUFFERS];
    uint8_t frame_buffer_count;
    // Size of each buffer in frame_buffers, in bytes - used to range-check whether a given
    // draw_bitmap() color_data pointer is actually one of them (see draw_bitmap() below).
    size_t frame_buffer_size_bytes;
    // Signaled by on_refresh_done once per real scan-out of a whole frame. Only waited on in
    // draw_bitmap() when color_data is one of frame_buffers and avoid_tearing is set - see the
    // comment there for why.
    SemaphoreHandle_t frame_complete_semaphore;
    // Heap-allocated only when the devicetree supplies a custom init_sequence (see
    // parse_init_sequence()) - nullptr otherwise, since the vendor's built-in default sequence
    // needs no parsing. Its .data pointers alias directly into the devicetree's static const
    // byte buffer, so only this struct array itself needs freeing in stop().
    jd9165_lcd_init_cmd_t* parsed_init_cmds;
};

// esp_lcd_dpi_panel's draw_bitmap() has a zero-copy path when color_data is one of the panel's
// own frame buffers (as returned by esp_lcd_dpi_panel_get_frame_buffer()): it just repoints which
// buffer is scanned out and returns almost instantly - well before the DSI peripheral has
// actually finished scanning out the *previous* buffer, let alone started on this one. Callers in
// full/direct LVGL render mode render straight into these real frame buffers, so if draw_bitmap()
// returned that quickly, LVGL would be free to start overwriting the *other* buffer - which may
// still be mid-scanout - producing visible tearing/flashing. on_refresh_done fires once per actual
// whole-frame scan-out completion (continuously, at the panel's refresh rate, independent of
// draw_bitmap calls), so waiting for the next occurrence after each draw_bitmap() genuinely blocks
// until it's safe to start writing into the frame buffers again - unless avoid_tearing is false,
// in which case the caller has opted out of this wait (see the binding property).
static bool on_refresh_done(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t*, void* user_ctx) {
    auto* internal = static_cast<Jd9165Internal*>(user_ctx);
    BaseType_t high_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(internal->frame_complete_semaphore, &high_task_woken);
    return high_task_woken == pdTRUE;
}

static int pin_or_unused(const GpioPinSpec& pin) {
    return pin.gpio_controller == nullptr ? -1 : static_cast<int>(pin.pin);
}

static lcd_color_format_t color_format_from_bits_per_pixel(uint8_t bits_per_pixel) {
    switch (bits_per_pixel) {
        case 18: return LCD_COLOR_FMT_RGB666;
        case 24: return LCD_COLOR_FMT_RGB888;
        default: return LCD_COLOR_FMT_RGB565;
    }
}

// Unpacks the devicetree's flat [cmd, data_len, delay_ms, data_len bytes...] encoding (produced
// by the devicetree compiler's "array" property type - see init-sequence in
// bindings/jdi,jd9165.yaml) into a heap-allocated jd9165_lcd_init_cmd_t array. Each entry's .data
// points directly into `bytes`, which is the devicetree's static const buffer and outlives the
// device, so no per-entry copy is needed.
static bool parse_init_sequence(const uint8_t* bytes, uint32_t length, jd9165_lcd_init_cmd_t** out_cmds, uint16_t* out_count) {
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

    auto* cmds = static_cast<jd9165_lcd_init_cmd_t*>(malloc(count * sizeof(jd9165_lcd_init_cmd_t)));
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

    auto* internal = static_cast<Jd9165Internal*>(malloc(sizeof(Jd9165Internal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    internal->parsed_init_cmds = nullptr;

    const jd9165_lcd_init_cmd_t* init_cmds = nullptr;
    uint16_t init_cmds_size = 0;
    if (config->init_sequence != nullptr && config->init_sequence_length > 0) {
        if (!parse_init_sequence(config->init_sequence, config->init_sequence_length, &internal->parsed_init_cmds, &init_cmds_size)) {
            LOG_E(TAG, "Failed to parse init-sequence property");
            free(internal);
            return ERROR_INVALID_ARGUMENT;
        }
        init_cmds = internal->parsed_init_cmds;
    }

    // The MIPI DSI PHY has no power of its own until this LDO channel is enabled - must happen
    // before the DSI bus is created.
    esp_ldo_channel_config_t ldo_config = {
        .chan_id = config->ldo_channel,
        .voltage_mv = config->ldo_voltage_mv,
        .flags = {},
    };
    if (esp_ldo_acquire_channel(&ldo_config, &internal->ldo_handle) != ESP_OK) {
        LOG_E(TAG, "Failed to acquire LDO channel for MIPI DSI PHY");
        free(internal->parsed_init_cmds);
        free(internal);
        return ERROR_RESOURCE;
    }

    const esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = config->dsi_bus_id,
        .num_data_lanes = config->num_data_lanes,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = config->lane_bit_rate_mbps,
    };
    if (esp_lcd_new_dsi_bus(&bus_config, &internal->dsi_bus_handle) != ESP_OK) {
        LOG_E(TAG, "Failed to create MIPI DSI bus");
        esp_ldo_release_channel(internal->ldo_handle);
        free(internal->parsed_init_cmds);
        free(internal);
        return ERROR_RESOURCE;
    }

    const esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    if (esp_lcd_new_panel_io_dbi(internal->dsi_bus_handle, &dbi_config, &internal->io_handle) != ESP_OK) {
        LOG_E(TAG, "Failed to create panel IO");
        esp_lcd_del_dsi_bus(internal->dsi_bus_handle);
        esp_ldo_release_channel(internal->ldo_handle);
        free(internal->parsed_init_cmds);
        free(internal);
        return ERROR_RESOURCE;
    }

    const lcd_color_format_t color_format = color_format_from_bits_per_pixel(config->bits_per_pixel);
    const esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = config->dpi_clock_freq_mhz,
        .pixel_format = (lcd_color_rgb_pixel_format_t)0, // deprecated field - in/out_color_format below take precedence
        .in_color_format = color_format,
        .out_color_format = color_format,
        .num_fbs = config->num_fbs,
        .video_timing = {
            .h_size = config->horizontal_resolution,
            .v_size = config->vertical_resolution,
            .hsync_pulse_width = config->hsync_pulse_width,
            .hsync_back_porch = config->hsync_back_porch,
            .hsync_front_porch = config->hsync_front_porch,
            .vsync_pulse_width = config->vsync_pulse_width,
            .vsync_back_porch = config->vsync_back_porch,
            .vsync_front_porch = config->vsync_front_porch,
        },
        .flags = {
            .use_dma2d = config->use_dma2d,
            .disable_lp = config->disable_lp,
        },
    };

    jd9165_vendor_config_t vendor_config = {
        .init_cmds = init_cmds,
        .init_cmds_size = init_cmds_size,
        .mipi_config = {
            .dsi_bus = internal->dsi_bus_handle,
            .dpi_config = &dpi_config,
        },
    };

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = pin_or_unused(config->pin_reset),
        .rgb_ele_order = config->bgr_order ? LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = config->bits_per_pixel,
        .flags = { .reset_active_high = config->reset_active_high },
        .vendor_config = &vendor_config,
    };

    if (esp_lcd_new_panel_jd9165(internal->io_handle, &panel_config, &internal->panel_handle) != ESP_OK) {
        LOG_E(TAG, "Failed to create panel");
        esp_lcd_panel_io_del(internal->io_handle);
        esp_lcd_del_dsi_bus(internal->dsi_bus_handle);
        esp_ldo_release_channel(internal->ldo_handle);
        free(internal->parsed_init_cmds);
        free(internal);
        return ERROR_RESOURCE;
    }

    // Bring-up sequence: reset() pulses (or software-resets) the panel and init() pushes
    // init_cmds over the DBI command interface before bringing up the underlying DPI peripheral.
    // swap_xy/set_gap are intentionally not called: the JD9165 driver doesn't override them and
    // the underlying raw DPI panel doesn't implement them either, so both would just fail with
    // ESP_ERR_NOT_SUPPORTED (see DisplayApi below). Every failure path here must clean up fully:
    // unlike stop_device, this is never retried by the kernel if start_device fails (see
    // device_start() in TactilityKernel), so a partial failure here would leak.
    bool ok =
        esp_lcd_panel_reset(internal->panel_handle) == ESP_OK &&
        esp_lcd_panel_init(internal->panel_handle) == ESP_OK &&
        esp_lcd_panel_invert_color(internal->panel_handle, config->invert_color) == ESP_OK &&
        esp_lcd_panel_mirror(internal->panel_handle, config->mirror_x, config->mirror_y) == ESP_OK &&
        esp_lcd_panel_disp_on_off(internal->panel_handle, true) == ESP_OK;

    if (!ok) {
        LOG_E(TAG, "Failed to bring up panel");
        esp_lcd_panel_del(internal->panel_handle);
        esp_lcd_panel_io_del(internal->io_handle);
        esp_lcd_del_dsi_bus(internal->dsi_bus_handle);
        esp_ldo_release_channel(internal->ldo_handle);
        free(internal->parsed_init_cmds);
        free(internal);
        return ERROR_RESOURCE;
    }

    internal->frame_buffer_count = 0;
    internal->frame_buffer_size_bytes = (size_t)config->horizontal_resolution * config->vertical_resolution *
        ((config->bits_per_pixel + 7) / 8);
    if (config->num_fbs > 0) {
        // esp_lcd_dpi_panel_get_frame_buffer() is variadic: the number of out-pointer arguments
        // passed must match fb_num exactly, so this can't be a loop.
        size_t fb_num = config->num_fbs < MAX_CACHED_FRAME_BUFFERS ? config->num_fbs : MAX_CACHED_FRAME_BUFFERS;
        esp_err_t ret;
        switch (fb_num) {
            case 1:
                ret = esp_lcd_dpi_panel_get_frame_buffer(internal->panel_handle, 1, &internal->frame_buffers[0]);
                break;
            case 2:
                ret = esp_lcd_dpi_panel_get_frame_buffer(internal->panel_handle, 2, &internal->frame_buffers[0], &internal->frame_buffers[1]);
                break;
            default:
                ret = ESP_OK;
                break;
        }

        if (ret != ESP_OK) {
            LOG_E(TAG, "Failed to get frame buffer(s): %s", esp_err_to_name(ret));
            esp_lcd_panel_del(internal->panel_handle);
            esp_lcd_panel_io_del(internal->io_handle);
            esp_lcd_del_dsi_bus(internal->dsi_bus_handle);
            esp_ldo_release_channel(internal->ldo_handle);
            free(internal->parsed_init_cmds);
            free(internal);
            return ERROR_RESOURCE;
        }
        internal->frame_buffer_count = (uint8_t)fb_num;
    }

    internal->frame_complete_semaphore = xSemaphoreCreateBinary();
    if (internal->frame_complete_semaphore == nullptr) {
        esp_lcd_panel_del(internal->panel_handle);
        esp_lcd_panel_io_del(internal->io_handle);
        esp_lcd_del_dsi_bus(internal->dsi_bus_handle);
        esp_ldo_release_channel(internal->ldo_handle);
        free(internal->parsed_init_cmds);
        free(internal);
        return ERROR_OUT_OF_MEMORY;
    }

    esp_lcd_dpi_panel_event_callbacks_t callbacks = {};
    callbacks.on_refresh_done = on_refresh_done;
    if (esp_lcd_dpi_panel_register_event_callbacks(internal->panel_handle, &callbacks, internal) != ESP_OK) {
        LOG_E(TAG, "Failed to register panel event callbacks");
        vSemaphoreDelete(internal->frame_complete_semaphore);
        esp_lcd_panel_del(internal->panel_handle);
        esp_lcd_panel_io_del(internal->io_handle);
        esp_lcd_del_dsi_bus(internal->dsi_bus_handle);
        esp_ldo_release_channel(internal->ldo_handle);
        free(internal->parsed_init_cmds);
        free(internal);
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Jd9165Internal*>(device_get_driver_data(device));

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

    if (internal->dsi_bus_handle != nullptr) {
        if (esp_lcd_del_dsi_bus(internal->dsi_bus_handle) != ESP_OK) {
            LOG_E(TAG, "Failed to delete DSI bus");
            return ERROR_RESOURCE;
        }
        internal->dsi_bus_handle = nullptr;
    }

    if (internal->ldo_handle != nullptr) {
        if (esp_ldo_release_channel(internal->ldo_handle) != ESP_OK) {
            LOG_E(TAG, "Failed to release LDO channel");
            return ERROR_RESOURCE;
        }
        internal->ldo_handle = nullptr;
    }

    vSemaphoreDelete(internal->frame_complete_semaphore);
    free(internal->parsed_init_cmds);
    free(internal);
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

// endregion

// region DisplayApi

static error_t jd9165_reset(Device* device) {
    auto* internal = static_cast<Jd9165Internal*>(device_get_driver_data(device));
    return esp_lcd_panel_reset(internal->panel_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t jd9165_init(Device* device) {
    auto* internal = static_cast<Jd9165Internal*>(device_get_driver_data(device));
    return esp_lcd_panel_init(internal->panel_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

// Only block for scan-out completion when color_data is actually one of the panel's own frame
// buffers (see on_refresh_done's comment above for why that matters) and allow_tearing is not
// set - i.e. this specific call is a zero-copy flip, not a plain CPU copy into the panel's buffer
// from a caller-owned one (e.g. LVGL bound in owned-buffer mode), which has no reuse race to
// guard against and shouldn't pay the up-to-one-frame latency cost for every partial update.
static bool jd9165_color_data_is_frame_buffer(const Jd9165Internal* internal, const void* color_data) {
    const auto* ptr = static_cast<const uint8_t*>(color_data);
    for (uint8_t i = 0; i < internal->frame_buffer_count; i++) {
        const auto* base = static_cast<const uint8_t*>(internal->frame_buffers[i]);
        if (ptr >= base && ptr < base + internal->frame_buffer_size_bytes) {
            return true;
        }
    }
    return false;
}

static error_t jd9165_draw_bitmap(Device* device, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data) {
    auto* internal = static_cast<Jd9165Internal*>(device_get_driver_data(device));

    bool wait_for_scanout = !GET_CONFIG(device)->allow_tearing && jd9165_color_data_is_frame_buffer(internal, color_data);
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

// Mirror is always implemented by LCD command (MADCTL), unconditionally, by esp_lcd_jd9165 - see
// panel_jd9165_mirror() in esp_lcd_jd9165.c. Unlike esp_lcd_rgb_panel's software rotate_mask
// trick, this isn't tied to draw_bitmap's copy path, so it stays available even when LVGL is
// bound directly onto the panel's own frame buffers.
static error_t jd9165_mirror(Device* device, bool x_axis, bool y_axis) {
    auto* internal = static_cast<Jd9165Internal*>(device_get_driver_data(device));
    return esp_lcd_panel_mirror(internal->panel_handle, x_axis, y_axis) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static bool jd9165_get_mirror_x(Device* device) {
    return GET_CONFIG(device)->mirror_x;
}

static bool jd9165_get_mirror_y(Device* device) {
    return GET_CONFIG(device)->mirror_y;
}

// swap_xy/set_gap are not exposed: esp_lcd_jd9165 doesn't override them and the underlying raw
// MIPI DPI panel doesn't implement them either (esp_lcd_panel_dpi.c never assigns those function
// pointers), so esp_lcd_panel_swap_xy()/set_gap() would just return ESP_ERR_NOT_SUPPORTED.

static error_t jd9165_invert_color(Device* device, bool invert_color_data) {
    auto* internal = static_cast<Jd9165Internal*>(device_get_driver_data(device));
    return esp_lcd_panel_invert_color(internal->panel_handle, invert_color_data) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t jd9165_disp_on_off(Device* device, bool on_off) {
    auto* internal = static_cast<Jd9165Internal*>(device_get_driver_data(device));
    return esp_lcd_panel_disp_on_off(internal->panel_handle, on_off) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

// disp_sleep is not exposed: esp_lcd_jd9165 doesn't override it either (only
// del/init/reset/mirror/invert_color/disp_on_off - see esp_lcd_jd9165.c).

// bgr_order only selects the panel controller's rgb_ele_order (applied in start(), above) so the
// R/B swap happens on-chip. LVGL always fills the same-layout buffer either way - there's no
// separate "BGR" memory layout to produce, unlike the SPI byte-order swap some other panels need.
static enum DisplayColorFormat jd9165_get_color_format(Device* device) {
    return GET_CONFIG(device)->bits_per_pixel == 24 ? DISPLAY_COLOR_FORMAT_RGB888 : DISPLAY_COLOR_FORMAT_RGB565;
}

static uint16_t jd9165_get_resolution_x(Device* device) {
    return GET_CONFIG(device)->horizontal_resolution;
}

static uint16_t jd9165_get_resolution_y(Device* device) {
    return GET_CONFIG(device)->vertical_resolution;
}

static void jd9165_get_frame_buffer(Device* device, uint8_t index, void** out_buffer) {
    auto* internal = static_cast<Jd9165Internal*>(device_get_driver_data(device));
    *out_buffer = index < internal->frame_buffer_count ? internal->frame_buffers[index] : nullptr;
}

static uint8_t jd9165_get_frame_buffer_count(Device* device) {
    auto* internal = static_cast<Jd9165Internal*>(device_get_driver_data(device));
    return internal->frame_buffer_count;
}

static error_t jd9165_get_backlight(Device* device, Device** backlight) {
    auto* configured_backlight = GET_CONFIG(device)->backlight;
    if (configured_backlight == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    *backlight = configured_backlight;
    return ERROR_NONE;
}

// endregion

static const DisplayApi jd9165_display_api = {
    .capabilities = DISPLAY_CAPABILITY_CAP_MIRROR | DISPLAY_CAPABILITY_INVERT_COLOR |
        DISPLAY_CAPABILITY_ON_OFF | DISPLAY_CAPABILITY_BACKLIGHT,
    .reset = jd9165_reset,
    .init = jd9165_init,
    .draw_bitmap = jd9165_draw_bitmap,
    .mirror = jd9165_mirror,
    .swap_xy = nullptr,
    .get_swap_xy = nullptr,
    .get_mirror_x = jd9165_get_mirror_x,
    .get_mirror_y = jd9165_get_mirror_y,
    .set_gap = nullptr,
    .invert_color = jd9165_invert_color,
    .disp_on_off = jd9165_disp_on_off,
    .disp_sleep = nullptr,
    .get_color_format = jd9165_get_color_format,
    .get_resolution_x = jd9165_get_resolution_x,
    .get_resolution_y = jd9165_get_resolution_y,
    .get_frame_buffer = jd9165_get_frame_buffer,
    .get_frame_buffer_count = jd9165_get_frame_buffer_count,
    .get_backlight = jd9165_get_backlight,
    .has_capability = nullptr,
};

Driver jd9165_driver = {
    .name = "jd9165",
    .compatible = (const char*[]) { "jdi,jd9165", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &jd9165_display_api,
    .device_type = &DISPLAY_TYPE,
    .owner = &jd9165_module,
    .internal = nullptr
};

#endif // SOC_MIPI_DSI_SUPPORTED
