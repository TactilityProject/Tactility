// SPDX-License-Identifier: Apache-2.0
#include <soc/soc_caps.h>
#if SOC_MIPI_DSI_SUPPORTED

#include <drivers/st7701_dsi.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/display.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <esp_err.h>
#include <esp_ldo_regulator.h>
#include <esp_lcd_mipi_dsi.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_st7701.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdlib>

#define TAG "St7701Dsi"
#define GET_CONFIG(device) (static_cast<const St7701DsiConfig*>((device)->config))

constexpr size_t MAX_CACHED_FRAME_BUFFERS = 2;

struct St7701DsiInternal {
    esp_ldo_channel_handle_t ldo_handle;
    esp_lcd_dsi_bus_handle_t dsi_bus_handle;
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_handle_t panel_handle;
    void* frame_buffers[MAX_CACHED_FRAME_BUFFERS];
    uint8_t frame_buffer_count;
    size_t frame_buffer_size_bytes;
    SemaphoreHandle_t frame_complete_semaphore;
    st7701_lcd_init_cmd_t* parsed_init_cmds;
};

static bool on_refresh_done(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t*, void* user_ctx) {
    auto* internal = static_cast<St7701DsiInternal*>(user_ctx);
    BaseType_t high_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(internal->frame_complete_semaphore, &high_task_woken);
    return high_task_woken == pdTRUE;
}

static int pin_or_unused(const GpioPinSpec& pin) {
    return pin.gpio_controller == nullptr ? -1 : static_cast<int>(pin.pin);
}

static lcd_color_format_t color_format_from_bits_per_pixel(uint8_t bits_per_pixel) {
    return bits_per_pixel == 24 ? LCD_COLOR_FMT_RGB888 : LCD_COLOR_FMT_RGB565;
}

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


static error_t start_device(Device* device) {
    const auto* config = GET_CONFIG(device);
    if (config->bits_per_pixel == 18) {
        LOG_E(TAG, "18bpp (RGB666) is unsupported on this ESP-IDF version");
        return ERROR_NOT_SUPPORTED;
    }

    auto* internal = static_cast<St7701DsiInternal*>(malloc(sizeof(St7701DsiInternal)));
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
        .lane_bit_rate_mbps = static_cast<float>(config->lane_bit_rate_mbps),
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
        .dpi_clock_freq_mhz = static_cast<float>(config->dpi_clock_freq_mhz),
        .in_color_format = color_format,
        .out_color_format = color_format,
        .num_fbs = static_cast<uint8_t>(config->num_fbs > MAX_CACHED_FRAME_BUFFERS ? MAX_CACHED_FRAME_BUFFERS : config->num_fbs),
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
            .disable_lp = config->disable_lp,
        },
    };

    st7701_vendor_config_t vendor_config = {
        .init_cmds = init_cmds,
        .init_cmds_size = init_cmds_size,
        .mipi_config = {
            .dsi_bus = internal->dsi_bus_handle,
            .dpi_config = &dpi_config,
        },
        .flags = {
            .use_mipi_interface = 1,
        },
    };

    const esp_lcd_panel_dev_config_t panel_config = {
        .rgb_ele_order = config->bgr_order ? LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = config->bits_per_pixel,
        .reset_gpio_num = static_cast<gpio_num_t>(pin_or_unused(config->pin_reset)),
        .vendor_config = &vendor_config,
        .flags = { .reset_active_high = false },
    };

    if (esp_lcd_new_panel_st7701(internal->io_handle, &panel_config, &internal->panel_handle) != ESP_OK) {
        LOG_E(TAG, "Failed to create panel");
        esp_lcd_panel_io_del(internal->io_handle);
        esp_lcd_del_dsi_bus(internal->dsi_bus_handle);
        esp_ldo_release_channel(internal->ldo_handle);
        free(internal->parsed_init_cmds);
        free(internal);
        return ERROR_RESOURCE;
    }

    if (config->use_dma2d) {
        esp_lcd_dpi_panel_enable_dma2d(internal->panel_handle);
    }

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
    callbacks.on_frame_buf_complete = on_refresh_done;
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

static error_t stop_device(Device* device) {
    auto* internal = static_cast<St7701DsiInternal*>(device_get_driver_data(device));

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



static error_t st7701_dsi_reset(Device* device) {
    auto* internal = static_cast<St7701DsiInternal*>(device_get_driver_data(device));
    return esp_lcd_panel_reset(internal->panel_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t st7701_dsi_init(Device* device) {
    auto* internal = static_cast<St7701DsiInternal*>(device_get_driver_data(device));
    return esp_lcd_panel_init(internal->panel_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static bool st7701_dsi_color_data_is_frame_buffer(const St7701DsiInternal* internal, const void* color_data) {
    const auto* ptr = static_cast<const uint8_t*>(color_data);
    for (uint8_t i = 0; i < internal->frame_buffer_count; i++) {
        const auto* base = static_cast<const uint8_t*>(internal->frame_buffers[i]);
        if (ptr >= base && ptr < base + internal->frame_buffer_size_bytes) {
            return true;
        }
    }
    return false;
}

static error_t st7701_dsi_draw_bitmap(Device* device, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data) {
    auto* internal = static_cast<St7701DsiInternal*>(device_get_driver_data(device));

    bool wait_for_scanout = !GET_CONFIG(device)->allow_tearing && st7701_dsi_color_data_is_frame_buffer(internal, color_data);
    if (wait_for_scanout) {
        xSemaphoreTake(internal->frame_complete_semaphore, 0); 
    }

    if (esp_lcd_panel_draw_bitmap(internal->panel_handle, x_start, y_start, x_end, y_end, color_data) != ESP_OK) {
        return ERROR_RESOURCE;
    }

    if (wait_for_scanout) {
        xSemaphoreTake(internal->frame_complete_semaphore, portMAX_DELAY);
    }

    return ERROR_NONE;
}

static error_t st7701_dsi_mirror(Device* device, bool x_axis, bool y_axis) {
    auto* internal = static_cast<St7701DsiInternal*>(device_get_driver_data(device));
    return esp_lcd_panel_mirror(internal->panel_handle, x_axis, y_axis) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static bool st7701_dsi_get_mirror_x(Device* device) {
    return GET_CONFIG(device)->mirror_x;
}

static bool st7701_dsi_get_mirror_y(Device* device) {
    return GET_CONFIG(device)->mirror_y;
}


static error_t st7701_dsi_invert_color(Device* device, bool invert_color_data) {
    auto* internal = static_cast<St7701DsiInternal*>(device_get_driver_data(device));
    return esp_lcd_panel_invert_color(internal->panel_handle, invert_color_data) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t st7701_dsi_disp_on_off(Device* device, bool on_off) {
    auto* internal = static_cast<St7701DsiInternal*>(device_get_driver_data(device));
    return esp_lcd_panel_disp_on_off(internal->panel_handle, on_off) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t st7701_dsi_disp_sleep(Device* device, bool sleep) {
    auto* internal = static_cast<St7701DsiInternal*>(device_get_driver_data(device));
    return esp_lcd_panel_disp_sleep(internal->panel_handle, sleep) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static enum DisplayColorFormat st7701_dsi_get_color_format(Device* device) {
    return GET_CONFIG(device)->bits_per_pixel == 24 ? DISPLAY_COLOR_FORMAT_RGB888 : DISPLAY_COLOR_FORMAT_RGB565;
}

static uint16_t st7701_dsi_get_resolution_x(Device* device) {
    return GET_CONFIG(device)->horizontal_resolution;
}

static uint16_t st7701_dsi_get_resolution_y(Device* device) {
    return GET_CONFIG(device)->vertical_resolution;
}

static void st7701_dsi_get_frame_buffer(Device* device, uint8_t index, void** out_buffer) {
    auto* internal = static_cast<St7701DsiInternal*>(device_get_driver_data(device));
    *out_buffer = index < internal->frame_buffer_count ? internal->frame_buffers[index] : nullptr;
}

static uint8_t st7701_dsi_get_frame_buffer_count(Device* device) {
    auto* internal = static_cast<St7701DsiInternal*>(device_get_driver_data(device));
    return internal->frame_buffer_count;
}

static error_t st7701_dsi_get_backlight(Device* device, Device** backlight) {
    auto* configured_backlight = GET_CONFIG(device)->backlight;
    if (configured_backlight == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    *backlight = configured_backlight;
    return ERROR_NONE;
}


static const DisplayApi st7701_dsi_display_api = {
    .capabilities = DISPLAY_CAPABILITY_CAP_MIRROR | DISPLAY_CAPABILITY_INVERT_COLOR |
        DISPLAY_CAPABILITY_ON_OFF | DISPLAY_CAPABILITY_BACKLIGHT | DISPLAY_CAPABILITY_SLEEP,
    .reset = st7701_dsi_reset,
    .init = st7701_dsi_init,
    .draw_bitmap = st7701_dsi_draw_bitmap,
    .clear = nullptr,
    .refresh = nullptr,
    .mirror = st7701_dsi_mirror,
    .swap_xy = nullptr,
    .get_swap_xy = nullptr,
    .get_mirror_x = st7701_dsi_get_mirror_x,
    .get_mirror_y = st7701_dsi_get_mirror_y,
    .set_gap = nullptr,
    .get_gap_x = nullptr,
    .get_gap_y = nullptr,
    .invert_color = st7701_dsi_invert_color,
    .disp_on_off = st7701_dsi_disp_on_off,
    .disp_sleep = st7701_dsi_disp_sleep,
    .get_color_format = st7701_dsi_get_color_format,
    .get_resolution_x = st7701_dsi_get_resolution_x,
    .get_resolution_y = st7701_dsi_get_resolution_y,
    .get_frame_buffer = st7701_dsi_get_frame_buffer,
    .get_frame_buffer_count = st7701_dsi_get_frame_buffer_count,
    .get_backlight = st7701_dsi_get_backlight,
    .has_capability = nullptr,
};

Driver st7701_dsi_driver = {
    .name = "st7701_dsi",
    .compatible = (const char*[]) { "sitronix,st7701-dsi", nullptr },
    .start_device = start_device,
    .stop_device = stop_device,
    .probe = nullptr,
    .api = &st7701_dsi_display_api,
    .device_type = &DISPLAY_TYPE,
    .internal = nullptr
};

#endif // SOC_MIPI_DSI_SUPPORTED
