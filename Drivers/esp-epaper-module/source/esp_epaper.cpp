// SPDX-License-Identifier: Apache-2.0
#include <drivers/esp_epaper.h>
#include <esp_epaper_module.h>
#include "esp_epaper_rotate.h"

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/esp32_spi.h>
#include <tactility/drivers/spi_controller.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <epaper.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdlib>
#include <cstring>

constexpr auto* TAG = "esp_epaper";
#define GET_CONFIG(device) (static_cast<const EspEpaperConfig*>((device)->config))

/** Width/height overrides above this are rejected as nonsense. */
constexpr uint16_t MAX_PANEL_DIMENSION = 2048;

struct EspEpaperInternal {
    Device* spi_controller;
    /** Opaque esp_epaper device, owns the panel's pins and SPI device. */
    epd_handle_t epd;
    epd_panel_info_t info;
    /** Scratch buffer in native panel layout, for rotated frames (rotation != 0). */
    uint8_t* rotate_buffer;
    /** Serializes panel/SPI access between draw_bitmap and power state changes. */
    SemaphoreHandle_t panel_mutex;
    /** disp_on_off state; the panel is in deep sleep while false. */
    bool display_on;
};

static uint16_t esp_epaper_get_display_width(const EspEpaperInternal* internal, uint8_t rotation) {
    return esp_epaper_rotation_swaps_axes(rotation) ? internal->info.height : internal->info.width;
}

static uint16_t esp_epaper_get_display_height(const EspEpaperInternal* internal, uint8_t rotation) {
    return esp_epaper_rotation_swaps_axes(rotation) ? internal->info.width : internal->info.height;
}

static bool resolve_panel_type(const char* name, epd_panel_type_t* out_type) {
    struct PanelMapping {
        const char* name;
        epd_panel_type_t type;
    };
    static constexpr PanelMapping kPanelMappings[] = {
        { "gdey0154d67", EPD_PANEL_GDEY0154D67 },
        { "gdep073e01", EPD_PANEL_GDEP073E01 },
        { "gdey037f51", EPD_PANEL_GDEY037F51 },
        { "gdey029t71h", EPD_PANEL_GDEY029T71H },
        { "ssd16xx-154", EPD_PANEL_SSD16XX_154 },
        { "ssd16xx-213", EPD_PANEL_SSD16XX_213 },
        { "ssd16xx-266", EPD_PANEL_SSD16XX_266 },
        { "ssd16xx-270", EPD_PANEL_SSD16XX_270 },
        { "ssd16xx-290", EPD_PANEL_SSD16XX_290 },
        { "ssd16xx-370", EPD_PANEL_SSD16XX_370 },
        { "ssd16xx-420", EPD_PANEL_SSD16XX_420 },
    };
    for (const auto& mapping : kPanelMappings) {
        if (std::strcmp(name, mapping.name) == 0) {
            *out_type = mapping.type;
            return true;
        }
    }
    return false;
}

static error_t esp_epaper_reset(Device* device) {
    auto* internal = static_cast<EspEpaperInternal*>(device_get_driver_data(device));
    xSemaphoreTake(internal->panel_mutex, portMAX_DELAY);
    // epd_wake() toggles the reset pin and re-runs the full init sequence.
    spi_controller_lock(internal->spi_controller);
    const esp_err_t ret = epd_wake(internal->epd);
    spi_controller_unlock(internal->spi_controller);
    // epd_wake() re-inits the panel, so it is awake (and drawable) again.
    if (ret == ESP_OK) {
        internal->display_on = true;
    }
    xSemaphoreGive(internal->panel_mutex);
    return ret == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t esp_epaper_init(Device* device) {
    auto* internal = static_cast<EspEpaperInternal*>(device_get_driver_data(device));
    xSemaphoreTake(internal->panel_mutex, portMAX_DELAY);
    spi_controller_lock(internal->spi_controller);
    const esp_err_t ret = epd_wake(internal->epd);
    spi_controller_unlock(internal->spi_controller);
    if (ret == ESP_OK) {
        internal->display_on = true;
    }
    xSemaphoreGive(internal->panel_mutex);
    return ret == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

// LVGL only ever calls this with the full frame: DISPLAY_COLOR_FORMAT_MONOCHROME forces
// LV_DISPLAY_RENDER_MODE_FULL in the generic kernel LVGL bridge (lvgl_display.c), and FULL mode
// only presents (calls draw_bitmap) once per render cycle, with the complete 0,0..hres,vres rect.
// hres/vres are the rotated (display) resolution reported by get_resolution_*; color_data is
// row-major, MSB-first 1bpp (LVGL's LV_COLOR_FORMAT_I1 with the palette header already stripped by
// the caller); bit 1 = white, bit 0 = black. epd_update() expects exactly that layout and polarity.
static error_t esp_epaper_draw_bitmap(Device* device, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data) {
    auto* internal = static_cast<EspEpaperInternal*>(device_get_driver_data(device));
    const auto* config = GET_CONFIG(device);

    const uint16_t display_width = esp_epaper_get_display_width(internal, config->rotation);
    const uint16_t display_height = esp_epaper_get_display_height(internal, config->rotation);
    if (x_start != 0 || y_start != 0 || x_end != display_width || y_end != display_height) {
        LOG_W(TAG, "draw_bitmap: only full-frame draws are supported (got %ld,%ld..%ld,%ld)", (long)x_start, (long)y_start, (long)x_end, (long)y_end);
        return ERROR_NOT_SUPPORTED;
    }

    xSemaphoreTake(internal->panel_mutex, portMAX_DELAY);

    if (!internal->display_on) {
        // Display is off (deep sleep). Drop the frame; the mismatch is fine because the next
        // power-on triggers a full refresh of the current render anyway.
        xSemaphoreGive(internal->panel_mutex);
        return ERROR_NONE;
    }

    const uint8_t* source;
    if (config->rotation == 0) {
        source = static_cast<const uint8_t*>(color_data);
    } else {
        memset(internal->rotate_buffer, 0, internal->info.buffer_size);
        esp_epaper_rotate_frame(static_cast<const uint8_t*>(color_data), internal->rotate_buffer,
                                internal->info.width, internal->info.height, config->rotation);
        source = internal->rotate_buffer;
    }

    spi_controller_lock(internal->spi_controller);
    const esp_err_t ret = epd_update(internal->epd, source, config->update_mode);
    spi_controller_unlock(internal->spi_controller);
    xSemaphoreGive(internal->panel_mutex);
    if (ret != ESP_OK) {
        LOG_E(TAG, "epd_update failed: %s", esp_err_to_name(ret));
        return ERROR_RESOURCE;
    }
    return ERROR_NONE;
}

static error_t esp_epaper_disp_on_off(Device* device, bool on_off) {
    auto* internal = static_cast<EspEpaperInternal*>(device_get_driver_data(device));

    xSemaphoreTake(internal->panel_mutex, portMAX_DELAY);
    if (on_off == internal->display_on) {
        xSemaphoreGive(internal->panel_mutex);
        return ERROR_NONE;
    }

    bool ok = true;
    spi_controller_lock(internal->spi_controller);
    if (on_off) {
        if (epd_wake(internal->epd) != ESP_OK) {
            LOG_E(TAG, "epd_wake failed");
            ok = false;
        }
    } else {
        if (epd_sleep(internal->epd) != ESP_OK) {
            LOG_E(TAG, "epd_sleep failed");
            ok = false;
        }
    }
    spi_controller_unlock(internal->spi_controller);

    if (ok) {
        internal->display_on = on_off;
    }
    xSemaphoreGive(internal->panel_mutex);
    return ok ? ERROR_NONE : ERROR_RESOURCE;
}

static DisplayColorFormat esp_epaper_get_color_format(Device*) {
    return DISPLAY_COLOR_FORMAT_MONOCHROME;
}

static uint16_t esp_epaper_get_resolution_x(Device* device) {
    auto* internal = static_cast<EspEpaperInternal*>(device_get_driver_data(device));
    return esp_epaper_get_display_width(internal, GET_CONFIG(device)->rotation);
}

static uint16_t esp_epaper_get_resolution_y(Device* device) {
    auto* internal = static_cast<EspEpaperInternal*>(device_get_driver_data(device));
    return esp_epaper_get_display_height(internal, GET_CONFIG(device)->rotation);
}

static void esp_epaper_get_frame_buffer(Device*, uint8_t, void** out_buffer) {
    *out_buffer = nullptr;
}

static uint8_t esp_epaper_get_frame_buffer_count(Device*) {
    return 0;
}

static const DisplayApi esp_epaper_display_api = {
    .capabilities = DISPLAY_CAPABILITY_ON_OFF | DISPLAY_CAPABILITY_SLOW_REFRESH,
    .reset = esp_epaper_reset,
    .init = esp_epaper_init,
    .draw_bitmap = esp_epaper_draw_bitmap,
    .clear = nullptr,
    .refresh = nullptr,
    .mirror = nullptr,
    .swap_xy = nullptr,
    .get_swap_xy = nullptr,
    .get_mirror_x = nullptr,
    .get_mirror_y = nullptr,
    .set_gap = nullptr,
    .get_gap_x = nullptr,
    .get_gap_y = nullptr,
    .invert_color = nullptr,
    .disp_on_off = esp_epaper_disp_on_off,
    .disp_sleep = nullptr,
    .get_color_format = esp_epaper_get_color_format,
    .get_resolution_x = esp_epaper_get_resolution_x,
    .get_resolution_y = esp_epaper_get_resolution_y,
    .get_frame_buffer = esp_epaper_get_frame_buffer,
    .get_frame_buffer_count = esp_epaper_get_frame_buffer_count,
    .get_backlight = nullptr,
    .has_capability = nullptr,
};

static void free_internal(EspEpaperInternal* internal) {
    if (internal->epd != nullptr) {
        spi_controller_lock(internal->spi_controller);
        epd_deinit(internal->epd);
        spi_controller_unlock(internal->spi_controller);
    }
    if (internal->rotate_buffer != nullptr) {
        free(internal->rotate_buffer);
    }
    if (internal->panel_mutex != nullptr) {
        vSemaphoreDelete(internal->panel_mutex);
    }
    free(internal);
}

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    check(device_get_type(parent) == &SPI_CONTROLLER_TYPE);

    const auto* spi_config = static_cast<const Esp32SpiConfig*>(parent->config);
    const auto* config = GET_CONFIG(device);

    epd_panel_type_t panel_type;
    if (!resolve_panel_type(config->panel_type, &panel_type)) {
        LOG_E(TAG, "Unknown panel type: %s", config->panel_type);
        return ERROR_NOT_SUPPORTED;
    }

    if (config->clock_speed_hz == 0) {
        LOG_E(TAG, "Invalid clock_speed_hz (0)");
        return ERROR_NOT_SUPPORTED;
    }

    if (config->update_mode > EPD_UPDATE_PARTIAL) {
        LOG_E(TAG, "Invalid update_mode %d (expected %d-%d)", (int)config->update_mode, (int)EPD_UPDATE_FULL, (int)EPD_UPDATE_PARTIAL);
        return ERROR_NOT_SUPPORTED;
    }

    if (config->width > MAX_PANEL_DIMENSION || config->height > MAX_PANEL_DIMENSION) {
        LOG_E(TAG, "Invalid panel size %ux%u (maximum %ux%u)", (unsigned)config->width, (unsigned)config->height, (unsigned)MAX_PANEL_DIMENSION, (unsigned)MAX_PANEL_DIMENSION);
        return ERROR_NOT_SUPPORTED;
    }

    epd_config_t epd_config = EPD_CONFIG_DEFAULT();
    epd_config.pins.busy = config->pin_busy.pin;
    epd_config.pins.rst = config->pin_reset.pin;
    epd_config.pins.dc = config->pin_dc.pin;
    epd_config.pins.cs = config->pin_cs.pin;
    epd_config.pins.sck = spi_config->pin_sclk.pin;
    epd_config.pins.mosi = spi_config->pin_mosi.pin;
    epd_config.spi.host = spi_config->host;
    epd_config.spi.speed_hz = config->clock_speed_hz;
    epd_config.panel.type = panel_type;
    epd_config.panel.width = config->width;
    epd_config.panel.height = config->height;

    epd_handle_t epd = nullptr;
    if (epd_init(&epd_config, &epd) != ESP_OK) {
        LOG_E(TAG, "epd_init failed");
        return ERROR_RESOURCE;
    }

    auto* internal = static_cast<EspEpaperInternal*>(calloc(1, sizeof(EspEpaperInternal)));
    if (internal == nullptr) {
        epd_deinit(epd);
        return ERROR_OUT_OF_MEMORY;
    }

    internal->spi_controller = parent;
    internal->epd = epd;
    internal->panel_mutex = xSemaphoreCreateMutex();
    if (internal->panel_mutex == nullptr) {
        free_internal(internal);
        return ERROR_OUT_OF_MEMORY;
    }

    if (epd_get_info(epd, &internal->info) != ESP_OK) {
        LOG_E(TAG, "epd_get_info failed");
        free_internal(internal);
        return ERROR_RESOURCE;
    }

    if (internal->info.color_mode != EPD_COLOR_BW) {
        LOG_E(TAG, "Panel color mode %d is not supported by the kernel display bridge (monochrome only)", internal->info.color_mode);
        free_internal(internal);
        return ERROR_NOT_SUPPORTED;
    }

    if (config->rotation > 3) {
        LOG_E(TAG, "Invalid rotation %u (expected 0-3)", config->rotation);
        free_internal(internal);
        return ERROR_NOT_SUPPORTED;
    }

    if (config->rotation != 0) {
        internal->rotate_buffer = static_cast<uint8_t*>(malloc(internal->info.buffer_size));
        if (internal->rotate_buffer == nullptr) {
            LOG_E(TAG, "Failed to allocate %lu-byte rotation buffer", internal->info.buffer_size);
            free_internal(internal);
            return ERROR_OUT_OF_MEMORY;
        }
    }

    internal->display_on = true;
    device_set_driver_data(device, internal);

    LOG_I(TAG, "Started %ux%u panel (buffer %lu bytes, rotation %u)", internal->info.width, internal->info.height, internal->info.buffer_size, config->rotation);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<EspEpaperInternal*>(device_get_driver_data(device));

    xSemaphoreTake(internal->panel_mutex, portMAX_DELAY);
    if (internal->display_on) {
        // Leave the panel in deep sleep.
        spi_controller_lock(internal->spi_controller);
        epd_sleep(internal->epd);
        spi_controller_unlock(internal->spi_controller);
        internal->display_on = false;
    }
    xSemaphoreGive(internal->panel_mutex);

    free_internal(internal);
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

Driver esp_epaper_driver = {
    .name = "esp_epaper",
    .compatible = (const char*[]) { "tuanpmt,esp-epaper", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &esp_epaper_display_api,
    .device_type = &DISPLAY_TYPE,
    .owner = &esp_epaper_module,
    .internal = nullptr
};
