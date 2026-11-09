// SPDX-License-Identifier: Apache-2.0
#include <drivers/nv3031b.h>

#include <tactility/check.h>
#include <tactility/delay.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/esp32_spi.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/spi_controller.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <esp_err.h>
#include <esp_lcd_io_spi.h>
#include <esp_lcd_nv3031b.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

#include <freertos/semphr.h>

#include <cstdlib>

constexpr auto* TAG = "NV3031B";
#define GET_CONFIG(device) (static_cast<const Nv3031bConfig*>((device)->config))

struct Nv3031bInternal {
    Device* spi_controller;
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_handle_t panel_handle;
    // draw_bitmap() must block until the transfer physically completes
    SemaphoreHandle_t draw_done_semaphore;
};

static bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void* user_ctx) {
    auto* internal = static_cast<Nv3031bInternal*>(user_ctx);
    BaseType_t high_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(internal->draw_done_semaphore, &high_task_woken);
    return high_task_woken == pdTRUE;
}

static int pin_or_unused(const struct GpioPinSpec& pin) {
    return pin.gpio_controller == nullptr ? -1 : static_cast<int>(pin.pin);
}

// The reset line of the NV3031B is active-low
static error_t hardware_reset(const struct GpioPinSpec& pin) {
    if (pin.gpio_controller == nullptr) {
        return ERROR_NONE;
    }

    auto* reset = gpio_descriptor_acquire(pin.gpio_controller, pin.pin, GPIO_FLAG_DIRECTION_OUTPUT | GPIO_FLAG_ACTIVE_LOW, GPIO_OWNER_GPIO);
    if (reset == nullptr) {
        LOG_E(TAG, "Failed to acquire reset pin");
        return ERROR_RESOURCE;
    }

    error_t error = gpio_descriptor_set_level(reset, true);
    if (error == ERROR_NONE) {
        delay_millis(10);
        error = gpio_descriptor_set_level(reset, false);
    }
    gpio_descriptor_release(reset);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to pulse reset pin");
        return error;
    }

    delay_millis(120);
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

    error_t error = hardware_reset(config->pin_reset);
    if (error != ERROR_NONE) {
        return error;
    }

    auto* internal = static_cast<Nv3031bInternal*>(malloc(sizeof(Nv3031bInternal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    internal->spi_controller = parent;
    internal->draw_done_semaphore = xSemaphoreCreateBinary();
    if (internal->draw_done_semaphore == nullptr) {
        free(internal);
        return ERROR_OUT_OF_MEMORY;
    }

    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = static_cast<gpio_num_t>(pin_or_unused(cs_pin)),
        .dc_gpio_num = GPIO_NUM_NC,
        .spi_mode = 3,
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
            .psram_dma_direct = 0,
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

    nv3031b_vendor_config_t vendor_config = {
        .init_cmds = nullptr,
        .init_cmds_size = 0,
    };

    // The hardware reset is done above, so the panel only gets a software reset
    esp_lcd_panel_dev_config_t panel_config = {
        .rgb_ele_order = config->bgr_order ? LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
        .reset_gpio_num = GPIO_NUM_NC,
        .vendor_config = &vendor_config,
        .flags = { .reset_active_high = false },
    };

    ret = esp_lcd_new_panel_nv3031b(internal->io_handle, &panel_config, &internal->panel_handle);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to create panel: %s", esp_err_to_name(ret));
        esp_lcd_panel_io_del(internal->io_handle);
        vSemaphoreDelete(internal->draw_done_semaphore);
        free(internal);
        return ERROR_RESOURCE;
    }

    // Every failure path below must clean up fully: start_device is not retried by the kernel
    spi_controller_lock(internal->spi_controller);
    bool ok =
        esp_lcd_panel_reset(internal->panel_handle) == ESP_OK &&
        esp_lcd_panel_init(internal->panel_handle) == ESP_OK;
    ok = ok && ((config->gap_x == 0 && config->gap_y == 0) || esp_lcd_panel_set_gap(internal->panel_handle, config->gap_x, config->gap_y) == ESP_OK);
    ok = ok && (!config->swap_xy || esp_lcd_panel_swap_xy(internal->panel_handle, true) == ESP_OK);
    ok = ok && ((!config->mirror_x && !config->mirror_y) || esp_lcd_panel_mirror(internal->panel_handle, config->mirror_x, config->mirror_y) == ESP_OK);
    ok = ok && esp_lcd_panel_invert_color(internal->panel_handle, config->invert_color) == ESP_OK;
    ok = ok && esp_lcd_panel_disp_on_off(internal->panel_handle, true) == ESP_OK;
    spi_controller_unlock(internal->spi_controller);

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
    auto* internal = static_cast<Nv3031bInternal*>(device_get_driver_data(device));

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
    free(internal);
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

// endregion

// region DisplayApi

static error_t nv3031b_reset(Device* device) {
    auto* internal = static_cast<Nv3031bInternal*>(device_get_driver_data(device));
    spi_controller_lock(internal->spi_controller);
    error_t result = esp_lcd_panel_reset(internal->panel_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
    spi_controller_unlock(internal->spi_controller);
    return result;
}

static error_t nv3031b_init(Device* device) {
    auto* internal = static_cast<Nv3031bInternal*>(device_get_driver_data(device));
    spi_controller_lock(internal->spi_controller);
    error_t result = esp_lcd_panel_init(internal->panel_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
    spi_controller_unlock(internal->spi_controller);
    return result;
}

static error_t nv3031b_draw_bitmap(Device* device, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data) {
    auto* internal = static_cast<Nv3031bInternal*>(device_get_driver_data(device));

    xSemaphoreTake(internal->draw_done_semaphore, 0);

    spi_controller_lock(internal->spi_controller);
    esp_err_t ret = esp_lcd_panel_draw_bitmap(internal->panel_handle, x_start, y_start, x_end, y_end, color_data);
    if (ret != ESP_OK) {
        spi_controller_unlock(internal->spi_controller);
        return ERROR_RESOURCE;
    }

    // The bus lock is held until the transfer is done so no other bus user can interleave
    xSemaphoreTake(internal->draw_done_semaphore, portMAX_DELAY);
    spi_controller_unlock(internal->spi_controller);
    return ERROR_NONE;
}

static error_t nv3031b_mirror(Device* device, bool x_axis, bool y_axis) {
    auto* internal = static_cast<Nv3031bInternal*>(device_get_driver_data(device));
    spi_controller_lock(internal->spi_controller);
    error_t result = esp_lcd_panel_mirror(internal->panel_handle, x_axis, y_axis) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
    spi_controller_unlock(internal->spi_controller);
    return result;
}

static error_t nv3031b_swap_xy(Device* device, bool swap_axes) {
    auto* internal = static_cast<Nv3031bInternal*>(device_get_driver_data(device));
    spi_controller_lock(internal->spi_controller);
    error_t result = esp_lcd_panel_swap_xy(internal->panel_handle, swap_axes) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
    spi_controller_unlock(internal->spi_controller);
    return result;
}

static bool nv3031b_get_swap_xy(Device* device) {
    return GET_CONFIG(device)->swap_xy;
}

static bool nv3031b_get_mirror_x(Device* device) {
    return GET_CONFIG(device)->mirror_x;
}

static bool nv3031b_get_mirror_y(Device* device) {
    return GET_CONFIG(device)->mirror_y;
}

static error_t nv3031b_set_gap(Device* device, int32_t x_gap, int32_t y_gap) {
    auto* internal = static_cast<Nv3031bInternal*>(device_get_driver_data(device));
    spi_controller_lock(internal->spi_controller);
    error_t result = esp_lcd_panel_set_gap(internal->panel_handle, x_gap, y_gap) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
    spi_controller_unlock(internal->spi_controller);
    return result;
}

static int32_t nv3031b_get_gap_x(Device* device) {
    return GET_CONFIG(device)->gap_x;
}

static int32_t nv3031b_get_gap_y(Device* device) {
    return GET_CONFIG(device)->gap_y;
}

static error_t nv3031b_invert_color(Device* device, bool invert_color_data) {
    auto* internal = static_cast<Nv3031bInternal*>(device_get_driver_data(device));
    spi_controller_lock(internal->spi_controller);
    error_t result = esp_lcd_panel_invert_color(internal->panel_handle, invert_color_data) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
    spi_controller_unlock(internal->spi_controller);
    return result;
}

static error_t nv3031b_disp_on_off(Device* device, bool on_off) {
    auto* internal = static_cast<Nv3031bInternal*>(device_get_driver_data(device));
    spi_controller_lock(internal->spi_controller);
    error_t result = esp_lcd_panel_disp_on_off(internal->panel_handle, on_off) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
    spi_controller_unlock(internal->spi_controller);
    return result;
}

// Pixels go out big-endian on the bus
static enum DisplayColorFormat nv3031b_get_color_format(Device*) {
    return DISPLAY_COLOR_FORMAT_RGB565_SWAPPED;
}

static uint16_t nv3031b_get_resolution_x(Device* device) {
    return GET_CONFIG(device)->horizontal_resolution;
}

static uint16_t nv3031b_get_resolution_y(Device* device) {
    return GET_CONFIG(device)->vertical_resolution;
}

static error_t nv3031b_get_backlight(Device* device, Device** backlight) {
    auto* configured_backlight = GET_CONFIG(device)->backlight;
    if (configured_backlight == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    *backlight = configured_backlight;
    return ERROR_NONE;
}

// endregion

static const DisplayApi nv3031b_display_api = {
    .capabilities = DISPLAY_CAPABILITY_CAP_MIRROR | DISPLAY_CAPABILITY_CAP_SWAP_XY |
        DISPLAY_CAPABILITY_CAP_SET_GAP | DISPLAY_CAPABILITY_INVERT_COLOR | DISPLAY_CAPABILITY_ON_OFF |
        DISPLAY_CAPABILITY_BACKLIGHT,
    .reset = nv3031b_reset,
    .init = nv3031b_init,
    .draw_bitmap = nv3031b_draw_bitmap,
    .clear = nullptr,
    .refresh = nullptr,
    .mirror = nv3031b_mirror,
    .swap_xy = nv3031b_swap_xy,
    .get_swap_xy = nv3031b_get_swap_xy,
    .get_mirror_x = nv3031b_get_mirror_x,
    .get_mirror_y = nv3031b_get_mirror_y,
    .set_gap = nv3031b_set_gap,
    .get_gap_x = nv3031b_get_gap_x,
    .get_gap_y = nv3031b_get_gap_y,
    .invert_color = nv3031b_invert_color,
    .disp_on_off = nv3031b_disp_on_off,
    .disp_sleep = nullptr,
    .get_color_format = nv3031b_get_color_format,
    .get_resolution_x = nv3031b_get_resolution_x,
    .get_resolution_y = nv3031b_get_resolution_y,
    .get_frame_buffer = nullptr,
    .get_frame_buffer_count = nullptr,
    .get_backlight = nv3031b_get_backlight,
    .has_capability = nullptr,
    .get_shape = nullptr,
};

Driver nv3031b_driver = {
    .name = "nv3031b",
    .compatible = (const char*[]) { "newvision,nv3031b", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = &nv3031b_display_api,
    .device_type = &DISPLAY_TYPE,
    .internal = nullptr
};
