// SPDX-License-Identifier: Apache-2.0
#include <drivers/st7305.h>
#include <st7305_module.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/esp32_spi.h>
#include <tactility/drivers/spi_controller.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_lcd_io_spi.h>
#include <esp_lcd_panel_io.h>

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdlib>
#include <cstring>
#include <iterator>

#define TAG "ST7305"

#define GET_CONFIG(device) (static_cast<const St7305Config*>((device)->config))

constexpr uint8_t CMD_SLPOUT = 0x11;
constexpr uint8_t CMD_INVOFF = 0x20;
constexpr uint8_t CMD_INVON = 0x21;
constexpr uint8_t CMD_DISPOFF = 0x28;
constexpr uint8_t CMD_DISPON = 0x29;
constexpr uint8_t CMD_CASET = 0x2A;
constexpr uint8_t CMD_RASET = 0x2B;
constexpr uint8_t CMD_RAMWR = 0x2C;
constexpr uint8_t CMD_MADCTL = 0x36;
constexpr uint8_t CMD_LPM = 0x39;
constexpr uint8_t CMD_DFS = 0x3A;
constexpr uint8_t CMD_GATE_LINE = 0xB0;

// Each RAM byte holds 4 source pixels of 2 adjacent gate lines and each column address spans 3 bytes.
constexpr uint32_t BYTES_PER_COLUMN = 3;

struct St7305Internal {
    Device* spi_controller;
    esp_lcd_panel_io_handle_t io_handle;
    // Given from ISR context when a queued SPI transfer completes, so draw_bitmap() can return
    // only after the panel buffer is free to be overwritten.
    SemaphoreHandle_t draw_done_semaphore;
    uint8_t* panel_buffer;
    uint32_t row_bytes;
    uint32_t row_count;
};

struct InitCommand {
    uint8_t cmd;
    uint8_t data[10];
    uint8_t data_size;
    uint16_t delay_ms;
};

// Vendor sequence for the 2.9" panel. Gate line count, column and row addresses depend on the
// resolution and are sent separately.
static const InitCommand INIT_COMMANDS[] = {
    {0xD6, {0x13, 0x02}, 2, 0}, // NVM load control
    {0xD1, {0x01}, 1, 0}, // Booster enable
    {0xC0, {0x08, 0x06}, 2, 0}, // Gate voltage
    {0xC1, {0x3C, 0x3E, 0x3C, 0x3C}, 4, 0}, // VSHP
    {0xC2, {0x23, 0x21, 0x23, 0x23}, 4, 0}, // VSLP
    {0xC4, {0x5A, 0x5C, 0x5A, 0x5A}, 4, 0}, // VSHN
    {0xC5, {0x37, 0x35, 0x37, 0x37}, 4, 0}, // VSLN
    {0xB2, {0x05}, 1, 0}, // Frame rate control
    {0xB3, {0xE5, 0xF6, 0x17, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x71}, 10, 0}, // HPM gate EQ
    {0xB4, {0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45}, 8, 0}, // LPM gate EQ
    {0x62, {0x32, 0x03, 0x1F}, 3, 0}, // Gate timing
    {0xB7, {0x13}, 1, 0}, // Source EQ enable
};

static const InitCommand INIT_COMMANDS_SLEEP_OUT[] = {
    {CMD_SLPOUT, {}, 0, 100},
    {0xC9, {0x00}, 1, 0}, // Source voltage select
    {CMD_MADCTL, {0x00}, 1, 0},
    {CMD_DFS, {0x11}, 1, 0}, // Data format select
    {0xB9, {0x20}, 1, 0}, // Gamma mode
    {0xB8, {0x29}, 1, 0}, // Panel setting
    {0xD0, {0xFF}, 1, 0}, // Auto power down
};

static IRAM_ATTR bool on_color_trans_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void* user_ctx) {
    auto* internal = static_cast<St7305Internal*>(user_ctx);
    BaseType_t high_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(internal->draw_done_semaphore, &high_task_woken);
    return high_task_woken == pdTRUE;
}

static int pin_or_unused(const struct GpioPinSpec& pin) {
    return pin.gpio_controller == nullptr ? -1 : static_cast<int>(pin.pin);
}

static bool send_command(St7305Internal* internal, uint8_t cmd, const uint8_t* data, size_t data_size) {
    return esp_lcd_panel_io_tx_param(internal->io_handle, cmd, data, data_size) == ESP_OK;
}

static bool send_commands(St7305Internal* internal, const InitCommand* commands, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (!send_command(internal, commands[i].cmd, commands[i].data_size > 0 ? commands[i].data : nullptr, commands[i].data_size)) {
            LOG_E(TAG, "Failed to send command 0x%02X", commands[i].cmd);
            return false;
        }
        if (commands[i].delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(commands[i].delay_ms));
        }
    }
    return true;
}

static bool send_address_window(St7305Internal* internal, const St7305Config* config) {
    const uint8_t caset[] = {
        config->column_offset,
        static_cast<uint8_t>(config->column_offset + internal->row_bytes / BYTES_PER_COLUMN - 1)
    };
    const uint8_t raset[] = {0x00, static_cast<uint8_t>(internal->row_count - 1)};
    return send_command(internal, CMD_CASET, caset, sizeof(caset)) &&
        send_command(internal, CMD_RASET, raset, sizeof(raset));
}

static void perform_hardware_reset(const St7305Config* config) {
    int pin = pin_or_unused(config->pin_reset);
    if (pin < 0) {
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

    // Reset is active-low
    gpio_set_level(static_cast<gpio_num_t>(pin), 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(static_cast<gpio_num_t>(pin), 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

static bool send_init_sequence(St7305Internal* internal, const St7305Config* config) {
    const uint8_t gate_lines = static_cast<uint8_t>(config->horizontal_resolution / 4);
    return send_commands(internal, INIT_COMMANDS, std::size(INIT_COMMANDS)) &&
        send_command(internal, CMD_GATE_LINE, &gate_lines, 1) &&
        send_commands(internal, INIT_COMMANDS_SLEEP_OUT, std::size(INIT_COMMANDS_SLEEP_OUT)) &&
        send_address_window(internal, config) &&
        send_command(internal, CMD_LPM, nullptr, 0) &&
        send_command(internal, config->invert_color ? CMD_INVON : CMD_INVOFF, nullptr, 0);
}

// region Driver lifecycle

static void free_internal(St7305Internal* internal) {
    if (internal->io_handle != nullptr) {
        esp_lcd_panel_io_del(internal->io_handle);
    }
    if (internal->draw_done_semaphore != nullptr) {
        vSemaphoreDelete(internal->draw_done_semaphore);
    }
    heap_caps_free(internal->panel_buffer);
    free(internal);
}

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    check(device_get_type(parent) == &SPI_CONTROLLER_TYPE);

    const auto* spi_config = static_cast<const Esp32SpiConfig*>(parent->config);
    const auto* config = GET_CONFIG(device);

    // Gate pairs are addressed by an 8-bit row address
    if (config->horizontal_resolution == 0 || config->horizontal_resolution % 4 != 0 || config->horizontal_resolution > 512) {
        LOG_E(TAG, "Invalid horizontal resolution %u (must be a multiple of 4, maximum 512)", config->horizontal_resolution);
        return ERROR_NOT_SUPPORTED;
    }

    struct GpioPinSpec cs_pin;
    if (esp32_spi_get_cs_pin(device, &cs_pin) != ERROR_NONE) {
        LOG_E(TAG, "Failed to resolve CS pin");
        return ERROR_RESOURCE;
    }

    auto* internal = static_cast<St7305Internal*>(calloc(1, sizeof(St7305Internal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    internal->spi_controller = parent;
    internal->row_count = config->horizontal_resolution / 2;
    const uint32_t source_bytes = (config->vertical_resolution + 3) / 4;
    internal->row_bytes = (source_bytes + BYTES_PER_COLUMN - 1) / BYTES_PER_COLUMN * BYTES_PER_COLUMN;

    internal->panel_buffer = static_cast<uint8_t*>(heap_caps_malloc(internal->row_bytes * internal->row_count, MALLOC_CAP_DMA));
    internal->draw_done_semaphore = xSemaphoreCreateBinary();
    if (internal->panel_buffer == nullptr || internal->draw_done_semaphore == nullptr) {
        free_internal(internal);
        return ERROR_OUT_OF_MEMORY;
    }

    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = static_cast<gpio_num_t>(pin_or_unused(cs_pin)),
        .dc_gpio_num = static_cast<gpio_num_t>(pin_or_unused(config->pin_dc)),
        .spi_mode = 0,
        .pclk_hz = config->pixel_clock_hz,
        .trans_queue_depth = 10,
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
            .sio_mode = 0,
            .psram_dma_direct = 0,
            .lsb_first = 0,
            .cs_high_active = 0,
        },
    };

    esp_err_t ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)spi_config->host, &io_config, &internal->io_handle);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to create panel IO: %s", esp_err_to_name(ret));
        free_internal(internal);
        return ERROR_RESOURCE;
    }

    spi_controller_lock(internal->spi_controller);
    perform_hardware_reset(config);
    bool ok = send_init_sequence(internal, config) &&
        send_command(internal, CMD_DISPON, nullptr, 0);
    spi_controller_unlock(internal->spi_controller);

    if (!ok) {
        LOG_E(TAG, "Failed to bring up panel");
        free_internal(internal);
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<St7305Internal*>(device_get_driver_data(device));

    spi_controller_lock(internal->spi_controller);
    esp_err_t ret = esp_lcd_panel_io_del(internal->io_handle);
    spi_controller_unlock(internal->spi_controller);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to delete panel IO");
        return ERROR_RESOURCE;
    }
    internal->io_handle = nullptr;

    free_internal(internal);
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

// endregion

// region DisplayApi

static error_t st7305_reset(Device* device) {
    auto* internal = static_cast<St7305Internal*>(device_get_driver_data(device));
    spi_controller_lock(internal->spi_controller);
    perform_hardware_reset(GET_CONFIG(device));
    spi_controller_unlock(internal->spi_controller);
    return ERROR_NONE;
}

static error_t st7305_init(Device* device) {
    auto* internal = static_cast<St7305Internal*>(device_get_driver_data(device));
    spi_controller_lock(internal->spi_controller);
    bool ok = send_init_sequence(internal, GET_CONFIG(device));
    spi_controller_unlock(internal->spi_controller);
    return ok ? ERROR_NONE : ERROR_RESOURCE;
}

// Converts the row-major MSB-first 1bpp frame (bit 1 = white) into the panel's RAM layout:
// RAM row r holds gate lines x = 2r and 2r + 1, and each byte packs 4 vertically adjacent pixels of
// both lines, interleaved MSB-first as (y, 2r), (y, 2r + 1), (y + 1, 2r), ... with bit 1 = black.
static void convert_frame(const St7305Internal* internal, const St7305Config* config, const uint8_t* src) {
    const uint32_t width = config->horizontal_resolution;
    const uint32_t height = config->vertical_resolution;
    const uint32_t src_stride = (width + 7) / 8;

    memset(internal->panel_buffer, 0, internal->row_bytes * internal->row_count);

    for (uint32_t y = 0; y < height; y++) {
        const uint32_t panel_y = config->mirror_y ? height - 1 - y : y;
        const uint8_t* src_row = src + y * src_stride;
        for (uint32_t x = 0; x < width; x++) {
            if (src_row[x / 8] & (0x80 >> (x % 8))) {
                continue;
            }
            const uint32_t panel_x = config->mirror_x ? width - 1 - x : x;
            const uint32_t index = (panel_x / 2) * internal->row_bytes + panel_y / 4;
            internal->panel_buffer[index] |= 0x80 >> ((panel_y % 4) * 2 + (panel_x % 2));
        }
    }
}

static error_t st7305_draw_bitmap(Device* device, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data) {
    auto* internal = static_cast<St7305Internal*>(device_get_driver_data(device));
    const auto* config = GET_CONFIG(device);

    if (x_start != 0 || y_start != 0 || x_end != config->horizontal_resolution || y_end != config->vertical_resolution) {
        LOG_W(TAG, "draw_bitmap: only full-frame draws are supported (got %ld,%ld..%ld,%ld)", (long)x_start, (long)y_start, (long)x_end, (long)y_end);
        return ERROR_NOT_SUPPORTED;
    }

    spi_controller_lock(internal->spi_controller);
    // Sending the address window first waits for in-flight chunks of a previous failed transfer that still read panel_buffer
    bool ok = send_address_window(internal, config);
    if (ok) {
        convert_frame(internal, config, static_cast<const uint8_t*>(color_data));
        // Drain any signal left over from earlier command transfers, which complete through the same callback
        xSemaphoreTake(internal->draw_done_semaphore, 0);
        ok = esp_lcd_panel_io_tx_color(internal->io_handle, CMD_RAMWR, internal->panel_buffer, internal->row_bytes * internal->row_count) == ESP_OK;
    }
    if (ok) {
        xSemaphoreTake(internal->draw_done_semaphore, portMAX_DELAY);
    }
    spi_controller_unlock(internal->spi_controller);
    return ok ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t st7305_invert_color(Device* device, bool invert_color_data) {
    auto* internal = static_cast<St7305Internal*>(device_get_driver_data(device));
    spi_controller_lock(internal->spi_controller);
    bool ok = send_command(internal, invert_color_data ? CMD_INVON : CMD_INVOFF, nullptr, 0);
    spi_controller_unlock(internal->spi_controller);
    return ok ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t st7305_disp_on_off(Device* device, bool on_off) {
    auto* internal = static_cast<St7305Internal*>(device_get_driver_data(device));
    spi_controller_lock(internal->spi_controller);
    bool ok = send_command(internal, on_off ? CMD_DISPON : CMD_DISPOFF, nullptr, 0);
    spi_controller_unlock(internal->spi_controller);
    return ok ? ERROR_NONE : ERROR_RESOURCE;
}

static DisplayColorFormat st7305_get_color_format(Device*) {
    return DISPLAY_COLOR_FORMAT_MONOCHROME;
}

static uint16_t st7305_get_resolution_x(Device* device) {
    return GET_CONFIG(device)->horizontal_resolution;
}

static uint16_t st7305_get_resolution_y(Device* device) {
    return GET_CONFIG(device)->vertical_resolution;
}

// endregion

static const DisplayApi st7305_display_api = {
    .capabilities = DISPLAY_CAPABILITY_INVERT_COLOR | DISPLAY_CAPABILITY_ON_OFF | DISPLAY_CAPABILITY_REQUIRES_FULL_FRAME | DISPLAY_CAPABILITY_PREFER_EXTERNAL_RAM,
    .reset = st7305_reset,
    .init = st7305_init,
    .draw_bitmap = st7305_draw_bitmap,
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
    .invert_color = st7305_invert_color,
    .disp_on_off = st7305_disp_on_off,
    .disp_sleep = nullptr,
    .get_color_format = st7305_get_color_format,
    .get_resolution_x = st7305_get_resolution_x,
    .get_resolution_y = st7305_get_resolution_y,
    .get_frame_buffer = nullptr,
    .get_frame_buffer_count = nullptr,
    .get_backlight = nullptr,
    .has_capability = nullptr,
};

Driver st7305_driver = {
    .name = "st7305",
    .compatible = (const char*[]) {"sitronix,st7305", nullptr},
    .start_device = start,
    .stop_device = stop,
    .api = &st7305_display_api,
    .device_type = &DISPLAY_TYPE,
    .owner = &st7305_module,
    .internal = nullptr
};
