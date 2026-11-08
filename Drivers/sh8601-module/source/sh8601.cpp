// SPDX-License-Identifier: Apache-2.0
#include <drivers/sh8601.h>
#include <drivers/sh8601_backlight.h>
#include <sh8601_module.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/backlight.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/esp32_spi.h>
#include <tactility/drivers/spi_controller.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <esp_err.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_sh8601.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdlib>
#include <cstring>
#include <new>

constexpr auto* TAG = "SH8601";

#define GET_CONFIG(device) (static_cast<const Sh8601Config*>((device)->config))
#define GET_BACKLIGHT_CONFIG(device) (static_cast<const Sh8601BacklightConfig*>((device)->config))

// QSPI commands encode their opcode in the top byte of the 32-bit command word (see the SH8601
// component's tx_param()/tx_color()).
#define SH8601_QSPI_OPCODE_WRITE_CMD (0x02U << 24)
// Brightness command; the parameter byte is a 0-255 duty level (0 = off).
#define SH8601_CMD_BRIGHTNESS 0x51

// MIRROR/SWAP_XY are deliberately not advertised: the SH8601 component only implements a partial
// mirror (X only, no Y) and rejects swap_xy outright, so the LVGL bridge falls back to software
// rotation (see devices.cpp's can_hw_rotate).
#define SH8601_STATIC_CAPABILITIES (DISPLAY_CAPABILITY_CAP_SET_GAP | DISPLAY_CAPABILITY_INVERT_COLOR | \
    DISPLAY_CAPABILITY_ON_OFF | DISPLAY_CAPABILITY_PREFER_EXTERNAL_RAM)

struct Sh8601Internal {
    Device* spi_controller;
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_handle_t panel_handle;
    // draw_bitmap() must block until the transfer physically completes (DisplayApi's synchronous
    // contract); see gc9a01-module for the same pattern.
    SemaphoreHandle_t draw_done_semaphore;
    // Full-screen copy of what the panel is currently showing, in the panel's byte order. The
    // SH8601 requires an even-aligned draw window (even start coordinate and even width/height),
    // so when LVGL hands us an odd-aligned region the extra border pixels have to come from
    // somewhere - this is that somewhere.
    uint8_t* shadow;
    // Gather buffer for the (possibly expanded) aligned window handed to the panel.
    uint8_t* staging;
};

// Bring-up sequence from the Waveshare BSP (esp32_s3_touch_amoled_2_06.c), proven correct on real
// hardware. The component sends MADCTL/COLMOD itself before these.
static const uint8_t SH8601_INIT_DATA_11[] = {0x00};
static const uint8_t SH8601_INIT_DATA_C4[] = {0x80};
static const uint8_t SH8601_INIT_DATA_44[] = {0x01, 0xD1};
static const uint8_t SH8601_INIT_DATA_35[] = {0x00};
static const uint8_t SH8601_INIT_DATA_53[] = {0x20};
static const uint8_t SH8601_INIT_DATA_63[] = {0xFF};
static const uint8_t SH8601_INIT_DATA_51_OFF[] = {0x00};
static const uint8_t SH8601_INIT_DATA_2A[] = {0x00, 0x16, 0x01, 0xAF};
static const uint8_t SH8601_INIT_DATA_2B[] = {0x00, 0x00, 0x01, 0xF5};
static const uint8_t SH8601_INIT_DATA_29[] = {0x00};
static const uint8_t SH8601_INIT_DATA_51_ON[] = {0xFF};

static const sh8601_lcd_init_cmd_t SH8601_INIT_COMMANDS[] = {
    {0x11, SH8601_INIT_DATA_11, 0, 120},
    {0xC4, SH8601_INIT_DATA_C4, 1, 0},
    {0x44, SH8601_INIT_DATA_44, 2, 0},
    {0x35, SH8601_INIT_DATA_35, 1, 0},
    {0x53, SH8601_INIT_DATA_53, 1, 10},
    {0x63, SH8601_INIT_DATA_63, 1, 10},
    {0x51, SH8601_INIT_DATA_51_OFF, 1, 10},
    {0x2A, SH8601_INIT_DATA_2A, 4, 0},
    {0x2B, SH8601_INIT_DATA_2B, 4, 0},
    {0x29, SH8601_INIT_DATA_29, 0, 10},
    {0x51, SH8601_INIT_DATA_51_ON, 1, 0},
};

static bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void* user_ctx) {
    auto* internal = static_cast<Sh8601Internal*>(user_ctx);
    BaseType_t high_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(internal->draw_done_semaphore, &high_task_woken);
    return high_task_woken == pdTRUE;
}

static int pin_or_unused(const struct GpioPinSpec& pin) {
    return pin.gpio_controller == nullptr ? -1 : static_cast<int>(pin.pin);
}

// Sends an already even-aligned, tightly-packed region to the panel and blocks until the last
// chunk has physically completed.
static error_t present(Sh8601Internal* internal, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* data) {
    xSemaphoreTake(internal->draw_done_semaphore, 0);

    spi_controller_lock(internal->spi_controller);
    esp_err_t ret = esp_lcd_panel_draw_bitmap(internal->panel_handle, x_start, y_start, x_end, y_end, data);
    if (ret != ESP_OK) {
        spi_controller_unlock(internal->spi_controller);
        return ERROR_RESOURCE;
    }

    // Hold the bus lock across the wait too: the command/data phases aren't wrapped in their own
    // acquire_bus by esp_lcd_panel_io_spi, so another bus user could otherwise interleave with a
    // transfer that's still in flight.
    xSemaphoreTake(internal->draw_done_semaphore, portMAX_DELAY);
    spi_controller_unlock(internal->spi_controller);
    return ERROR_NONE;
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

    auto* internal = static_cast<Sh8601Internal*>(calloc(1, sizeof(Sh8601Internal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    internal->spi_controller = parent;
    internal->draw_done_semaphore = xSemaphoreCreateBinary();
    if (internal->draw_done_semaphore == nullptr) {
        free(internal);
        return ERROR_OUT_OF_MEMORY;
    }

    const size_t frame_buffer_size = static_cast<size_t>(config->horizontal_resolution) * config->vertical_resolution * 2;
    internal->shadow = static_cast<uint8_t*>(malloc(frame_buffer_size));
    internal->staging = static_cast<uint8_t*>(malloc(frame_buffer_size));
    if (internal->shadow == nullptr || internal->staging == nullptr) {
        LOG_E(TAG, "Failed to allocate frame buffers");
        free(internal->shadow);
        free(internal->staging);
        vSemaphoreDelete(internal->draw_done_semaphore);
        free(internal);
        return ERROR_OUT_OF_MEMORY;
    }
    std::memset(internal->shadow, 0, frame_buffer_size);

    // Built manually rather than with SH8601_PANEL_IO_QSPI_CONFIG(): the component's macro assigns
    // the plain int -1 to dc_gpio_num (gpio_num_t), which C++ rejects (unlike C).
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = static_cast<gpio_num_t>(pin_or_unused(cs_pin)),
        .dc_gpio_num = static_cast<gpio_num_t>(-1),
        .spi_mode = 0,
        .pclk_hz = config->pixel_clock_hz,
        .trans_queue_depth = config->transaction_queue_depth,
        .on_color_trans_done = on_color_trans_done,
        .user_ctx = internal,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .flags = {
            .dc_high_on_cmd = 0,
            .dc_low_on_data = 0,
            .dc_low_on_param = 0,
            .octal_mode = 0,
            .quad_mode = 1,
            .sio_mode = 0,
            // The LVGL color buffer lives in PSRAM (see PREFER_EXTERNAL_RAM). Without this the SPI
            // driver bounces every chunk through a freshly-allocated internal DMA buffer (up to the
            // bus's 64KB max-transfer-size), which fails with "Failed to allocate priv TX buffer"
            // once internal RAM is tight (e.g. during Wi-Fi). ESP32-S3 can DMA from PSRAM directly.
            .psram_dma_direct = 1,
            .lsb_first = 0,
            .cs_high_active = 0,
        },
    };

    esp_err_t ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)spi_config->host, &io_config, &internal->io_handle);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to create panel IO: %s", esp_err_to_name(ret));
        free(internal->shadow);
        free(internal->staging);
        vSemaphoreDelete(internal->draw_done_semaphore);
        free(internal);
        return ERROR_RESOURCE;
    }

    sh8601_vendor_config_t vendor_config = {
        .init_cmds = SH8601_INIT_COMMANDS,
        .init_cmds_size = sizeof(SH8601_INIT_COMMANDS) / sizeof(SH8601_INIT_COMMANDS[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };

    esp_lcd_panel_dev_config_t panel_config = {
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
        .reset_gpio_num = static_cast<gpio_num_t>(pin_or_unused(config->pin_reset)),
        .vendor_config = &vendor_config,
        // The SH8601's reset line is fixed active-low in hardware.
        .flags = { .reset_active_high = false },
    };

    ret = esp_lcd_new_panel_sh8601(internal->io_handle, &panel_config, &internal->panel_handle);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to create panel: %s", esp_err_to_name(ret));
        esp_lcd_panel_io_del(internal->io_handle);
        free(internal->shadow);
        free(internal->staging);
        vSemaphoreDelete(internal->draw_done_semaphore);
        free(internal);
        return ERROR_RESOURCE;
    }

    spi_controller_lock(internal->spi_controller);
    bool ok =
        esp_lcd_panel_reset(internal->panel_handle) == ESP_OK &&
        esp_lcd_panel_init(internal->panel_handle) == ESP_OK;
    if (ok) {
        ok = esp_lcd_panel_set_gap(internal->panel_handle, config->gap_x, config->gap_y) == ESP_OK;
    }
    ok = ok && (!config->invert_color || esp_lcd_panel_invert_color(internal->panel_handle, true) == ESP_OK);
    ok = ok && esp_lcd_panel_disp_on_off(internal->panel_handle, true) == ESP_OK;
    spi_controller_unlock(internal->spi_controller);

    if (!ok) {
        LOG_E(TAG, "Failed to bring up panel");
        esp_lcd_panel_del(internal->panel_handle);
        esp_lcd_panel_io_del(internal->io_handle);
        free(internal->shadow);
        free(internal->staging);
        vSemaphoreDelete(internal->draw_done_semaphore);
        free(internal);
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, internal);

    // Wipe the panel's undefined power-up GRAM content. Done in horizontal bands (rather than one
    // full-frame draw) so the panel IO never queues several large PSRAM-bounce chunks at once.
    constexpr int32_t CLEAR_BAND_HEIGHT = 32;
    for (int32_t y = 0; y < config->vertical_resolution; y += CLEAR_BAND_HEIGHT) {
        int32_t y_end = y + CLEAR_BAND_HEIGHT;
        if (y_end > config->vertical_resolution) {
            y_end = config->vertical_resolution;
        }
        if (present(internal, 0, y, config->horizontal_resolution, y_end, internal->shadow + static_cast<size_t>(y) * config->horizontal_resolution * 2) != ERROR_NONE) {
            LOG_W(TAG, "Failed to clear panel");
            break;
        }
    }

    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Sh8601Internal*>(device_get_driver_data(device));

    spi_controller_lock(internal->spi_controller);
    if (internal->panel_handle != nullptr) {
        if (esp_lcd_panel_del(internal->panel_handle) != ESP_OK) {
            LOG_E(TAG, "Failed to delete panel");
            spi_controller_unlock(internal->spi_controller);
            return ERROR_RESOURCE;
        }
        internal->panel_handle = nullptr;
    }

    if (internal->io_handle != nullptr) {
        if (esp_lcd_panel_io_del(internal->io_handle) != ESP_OK) {
            LOG_E(TAG, "Failed to delete panel IO");
            spi_controller_unlock(internal->spi_controller);
            return ERROR_RESOURCE;
        }
        internal->io_handle = nullptr;
    }
    spi_controller_unlock(internal->spi_controller);

    vSemaphoreDelete(internal->draw_done_semaphore);
    free(internal->shadow);
    free(internal->staging);
    free(internal);
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

// endregion

// region DisplayApi

static error_t sh8601_reset(Device* device) {
    auto* internal = static_cast<Sh8601Internal*>(device_get_driver_data(device));
    const auto* config = GET_CONFIG(device);
    spi_controller_lock(internal->spi_controller);
    error_t result = esp_lcd_panel_reset(internal->panel_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
    spi_controller_unlock(internal->spi_controller);
    if (result == ERROR_NONE) {
        // The reset drops the panel's image, so the shadow no longer describes it.
        std::memset(internal->shadow, 0, static_cast<size_t>(config->horizontal_resolution) * config->vertical_resolution * 2);
    }
    return result;
}

static error_t sh8601_init(Device* device) {
    auto* internal = static_cast<Sh8601Internal*>(device_get_driver_data(device));
    spi_controller_lock(internal->spi_controller);
    error_t result = esp_lcd_panel_init(internal->panel_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
    spi_controller_unlock(internal->spi_controller);
    return result;
}

static error_t sh8601_draw_bitmap(Device* device, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data) {
    auto* internal = static_cast<Sh8601Internal*>(device_get_driver_data(device));
    const auto* config = GET_CONFIG(device);
    const int32_t hres = config->horizontal_resolution;
    const int32_t vres = config->vertical_resolution;

    if (x_start < 0) x_start = 0;
    if (y_start < 0) y_start = 0;
    if (x_end > hres) x_end = hres;
    if (y_end > vres) y_end = vres;
    if (x_start >= x_end || y_start >= y_end) {
        return ERROR_NONE;
    }

    const size_t shadow_stride = static_cast<size_t>(hres) * 2;

    // Keep the shadow in sync with the pixels LVGL just handed us. Row-by-row because the incoming
    // region is tightly packed while the shadow spans the full screen.
    const int32_t width = x_end - x_start;
    const int32_t height = y_end - y_start;
    const auto* source = static_cast<const uint8_t*>(color_data);
    for (int32_t row = 0; row < height; row++) {
        std::memcpy(
            internal->shadow + static_cast<size_t>(y_start + row) * shadow_stride + static_cast<size_t>(x_start) * 2,
            source + static_cast<size_t>(row) * width * 2,
            static_cast<size_t>(width) * 2
        );
    }

    // Expand to the even-aligned window the SH8601 requires: even start, even width/height.
    int32_t aligned_x_start = x_start & ~1;
    int32_t aligned_y_start = y_start & ~1;
    int32_t aligned_x_end = (x_end + 1) & ~1;
    int32_t aligned_y_end = (y_end + 1) & ~1;
    if (aligned_x_end > hres) aligned_x_end = hres;
    if (aligned_y_end > vres) aligned_y_end = vres;

    const bool already_aligned =
        aligned_x_start == x_start && aligned_y_start == y_start &&
        aligned_x_end == x_end && aligned_y_end == y_end;

    const uint8_t* send_buffer;
    if (already_aligned) {
        send_buffer = source;
    } else {
        const int32_t aligned_width = aligned_x_end - aligned_x_start;
        uint8_t* destination = internal->staging;
        for (int32_t row = aligned_y_start; row < aligned_y_end; row++) {
            std::memcpy(
                destination,
                internal->shadow + static_cast<size_t>(row) * shadow_stride + static_cast<size_t>(aligned_x_start) * 2,
                static_cast<size_t>(aligned_width) * 2
            );
            destination += static_cast<size_t>(aligned_width) * 2;
        }
        send_buffer = internal->staging;
    }

    return present(internal, aligned_x_start, aligned_y_start, aligned_x_end, aligned_y_end, send_buffer);
}

static error_t sh8601_mirror(Device*, bool, bool) {
    return ERROR_NOT_SUPPORTED;
}

static error_t sh8601_swap_xy(Device*, bool) {
    return ERROR_NOT_SUPPORTED;
}

static error_t sh8601_set_gap(Device* device, int32_t x_gap, int32_t y_gap) {
    auto* internal = static_cast<Sh8601Internal*>(device_get_driver_data(device));
    spi_controller_lock(internal->spi_controller);
    error_t result = esp_lcd_panel_set_gap(internal->panel_handle, x_gap, y_gap) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
    spi_controller_unlock(internal->spi_controller);
    return result;
}

static int32_t sh8601_get_gap_x(Device* device) {
    return GET_CONFIG(device)->gap_x;
}

static int32_t sh8601_get_gap_y(Device* device) {
    return GET_CONFIG(device)->gap_y;
}

static error_t sh8601_invert_color(Device* device, bool invert_color_data) {
    auto* internal = static_cast<Sh8601Internal*>(device_get_driver_data(device));
    spi_controller_lock(internal->spi_controller);
    error_t result = esp_lcd_panel_invert_color(internal->panel_handle, invert_color_data) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
    spi_controller_unlock(internal->spi_controller);
    return result;
}

static error_t sh8601_disp_on_off(Device* device, bool on_off) {
    auto* internal = static_cast<Sh8601Internal*>(device_get_driver_data(device));
    spi_controller_lock(internal->spi_controller);
    error_t result = esp_lcd_panel_disp_on_off(internal->panel_handle, on_off) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
    spi_controller_unlock(internal->spi_controller);
    return result;
}

// The panel is big-endian while the OS is little-endian, so every pixel's two bytes are swapped.
static enum DisplayColorFormat sh8601_get_color_format(Device*) {
    return DISPLAY_COLOR_FORMAT_RGB565_SWAPPED;
}

static uint16_t sh8601_get_resolution_x(Device* device) {
    return GET_CONFIG(device)->horizontal_resolution;
}

static uint16_t sh8601_get_resolution_y(Device* device) {
    return GET_CONFIG(device)->vertical_resolution;
}

struct BacklightSearch {
    Device* found;
};

static bool find_backlight_child(Device* child, void* context) {
    auto* search = static_cast<BacklightSearch*>(context);
    if (device_get_type(child) == &BACKLIGHT_TYPE) {
        search->found = child;
        return false;
    }
    return true;
}

// The backlight is a child node of the display (see the sh8601-backlight binding). A phandle
// between the two would be a cycle the devicetree compiler can't emit in either order.
static Device* find_backlight(Device* device) {
    BacklightSearch search = { nullptr };
    device_for_each_child(device, &search, find_backlight_child);
    return search.found;
}

static error_t sh8601_get_backlight(Device* device, Device** backlight) {
    auto* found = find_backlight(device);
    if (found == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    *backlight = found;
    return ERROR_NONE;
}

// BACKLIGHT availability is per-board (a board could wire a panel without using the panel's own
// brightness command), so it can't live in the driver-wide static capability mask.
static bool sh8601_has_capability(Device* device, uint32_t capability) {
    uint32_t capabilities = SH8601_STATIC_CAPABILITIES;
    if (find_backlight(device) != nullptr) {
        capabilities |= DISPLAY_CAPABILITY_BACKLIGHT;
    }
    return (capabilities & capability) == capability;
}

static const DisplayApi sh8601_display_api = {
    .capabilities = SH8601_STATIC_CAPABILITIES,
    .reset = sh8601_reset,
    .init = sh8601_init,
    .draw_bitmap = sh8601_draw_bitmap,
    .clear = nullptr,
    .refresh = nullptr,
    .mirror = sh8601_mirror,
    .swap_xy = sh8601_swap_xy,
    .get_swap_xy = nullptr,
    .get_mirror_x = nullptr,
    .get_mirror_y = nullptr,
    .set_gap = sh8601_set_gap,
    .get_gap_x = sh8601_get_gap_x,
    .get_gap_y = sh8601_get_gap_y,
    .invert_color = sh8601_invert_color,
    .disp_on_off = sh8601_disp_on_off,
    .disp_sleep = nullptr,
    .get_color_format = sh8601_get_color_format,
    .get_resolution_x = sh8601_get_resolution_x,
    .get_resolution_y = sh8601_get_resolution_y,
    // No direct frame buffer access: the panel streams from an LVGL-owned buffer.
    .get_frame_buffer = nullptr,
    .get_frame_buffer_count = nullptr,
    .get_backlight = sh8601_get_backlight,
    .has_capability = sh8601_has_capability,
};

Driver sh8601_driver = {
    .name = "sh8601",
    .compatible = (const char*[]) { "shenghe,sh8601", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = &sh8601_display_api,
    .device_type = &DISPLAY_TYPE,
    .internal = nullptr
};

// endregion

// region Backlight

struct Sh8601BacklightInternal {
    uint8_t brightness;
};

static error_t sh8601_backlight_set_brightness(Device* device, uint8_t brightness) {
    // The backlight is a child of the display it dims.
    auto* display = device_get_parent(device);
    if (display == nullptr) {
        return ERROR_INVALID_STATE;
    }

    auto* display_internal = static_cast<Sh8601Internal*>(device_get_driver_data(display));
    if (display_internal == nullptr || display_internal->io_handle == nullptr) {
        return ERROR_INVALID_STATE;
    }

    uint32_t lcd_cmd = SH8601_CMD_BRIGHTNESS;
    lcd_cmd <<= 8;
    lcd_cmd |= SH8601_QSPI_OPCODE_WRITE_CMD;

    spi_controller_lock(display_internal->spi_controller);
    esp_err_t ret = esp_lcd_panel_io_tx_param(display_internal->io_handle, lcd_cmd, &brightness, 1);
    spi_controller_unlock(display_internal->spi_controller);

    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to set brightness: %s", esp_err_to_name(ret));
        return ERROR_RESOURCE;
    }

    static_cast<Sh8601BacklightInternal*>(device_get_driver_data(device))->brightness = brightness;
    return ERROR_NONE;
}

static error_t sh8601_backlight_set_brightness_default(Device* device) {
    return sh8601_backlight_set_brightness(device, GET_BACKLIGHT_CONFIG(device)->brightness_default);
}

static error_t sh8601_backlight_get_brightness(Device* device, uint8_t* out_brightness) {
    *out_brightness = static_cast<Sh8601BacklightInternal*>(device_get_driver_data(device))->brightness;
    return ERROR_NONE;
}

static uint8_t sh8601_backlight_get_min_brightness(Device*) {
    return 0;
}

static uint8_t sh8601_backlight_get_max_brightness(Device*) {
    return 255;
}

static const BacklightApi SH8601_BACKLIGHT_API = {
    .set_brightness = sh8601_backlight_set_brightness,
    .set_brightness_default = sh8601_backlight_set_brightness_default,
    .get_brightness = sh8601_backlight_get_brightness,
    .get_min_brightness = sh8601_backlight_get_min_brightness,
    .get_max_brightness = sh8601_backlight_get_max_brightness,
};

static error_t sh8601_backlight_start(Device* device) {
    if (device_get_parent(device) == nullptr) {
        return ERROR_INVALID_STATE;
    }

    auto* internal = new(std::nothrow) Sh8601BacklightInternal { .brightness = 0 };
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    device_set_driver_data(device, internal);

    sh8601_backlight_set_brightness_default(device); // Allowed to fail, we don't care about the result

    return ERROR_NONE;
}

static error_t sh8601_backlight_stop(Device* device) {
    sh8601_backlight_set_brightness(device, 0); // Allowed to fail, we don't care about the result

    auto* internal = static_cast<Sh8601BacklightInternal*>(device_get_driver_data(device));
    device_set_driver_data(device, nullptr);
    delete internal;

    return ERROR_NONE;
}

Driver sh8601_backlight_driver = {
    .name = "sh8601_backlight",
    .compatible = (const char*[]) { "sh8601-backlight", nullptr },
    .start_device = sh8601_backlight_start,
    .stop_device = sh8601_backlight_stop,
    .probe = nullptr,
    .api = &SH8601_BACKLIGHT_API,
    .device_type = &BACKLIGHT_TYPE,
    .internal = nullptr
};

// endregion
