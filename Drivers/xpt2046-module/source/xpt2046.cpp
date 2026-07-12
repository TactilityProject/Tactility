// SPDX-License-Identifier: Apache-2.0
#include <drivers/xpt2046.h>
#include <xpt2046_module.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/esp32_spi.h>
#include <tactility/drivers/pointer.h>
#include <tactility/drivers/spi_controller.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <esp_err.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_touch.h>
#include <esp_lcd_touch_xpt2046.h>

#include <cstdlib>

#define TAG "XPT2046"
#define GET_CONFIG(device) (static_cast<const Xpt2046Config*>((device)->config))

struct Xpt2046Internal {
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_touch_handle_t touch_handle;
};

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

    auto* internal = static_cast<Xpt2046Internal*>(malloc(sizeof(Xpt2046Internal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    const esp_lcd_panel_io_spi_config_t io_config = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(cs_pin.pin);
    esp_err_t ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)spi_config->host, &io_config, &internal->io_handle);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to create panel IO: %s", esp_err_to_name(ret));
        free(internal);
        return ERROR_RESOURCE;
    }

    const esp_lcd_touch_config_t touch_config = {
        .x_max = config->x_max,
        .y_max = config->y_max,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = config->swap_xy ? 1u : 0u,
            .mirror_x = config->mirror_x ? 1u : 0u,
            .mirror_y = config->mirror_y ? 1u : 0u,
        },
        .process_coordinates = nullptr,
        .interrupt_callback = nullptr,
        .user_data = nullptr,
        .driver_data = nullptr,
    };

    ret = esp_lcd_touch_new_spi_xpt2046(internal->io_handle, &touch_config, &internal->touch_handle);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to create touch handle: %s", esp_err_to_name(ret));
        esp_lcd_panel_io_del(internal->io_handle);
        free(internal);
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Xpt2046Internal*>(device_get_driver_data(device));

    bool ok = true;

    // esp_lcd_touch_del() only releases the touch-side resources; the panel IO handle is owned
    // separately and needs its own deletion.
    if (esp_lcd_touch_del(internal->touch_handle) != ESP_OK) {
        LOG_E(TAG, "Failed to delete touch handle");
        ok = false;
    }

    if (esp_lcd_panel_io_del(internal->io_handle) != ESP_OK) {
        LOG_E(TAG, "Failed to delete panel IO handle");
        ok = false;
    }

    free(internal);
    return ok ? ERROR_NONE : ERROR_RESOURCE;
}

// endregion

// region PointerApi

static error_t xpt2046_enter_sleep(Device* device) {
    auto* internal = static_cast<Xpt2046Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_enter_sleep(internal->touch_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t xpt2046_exit_sleep(Device* device) {
    auto* internal = static_cast<Xpt2046Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_exit_sleep(internal->touch_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t xpt2046_read_data(Device* device, TickType_t timeout) {
    (void)timeout; // esp_lcd_touch_read_data() has no timeout parameter
    auto* internal = static_cast<Xpt2046Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_read_data(internal->touch_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static bool xpt2046_get_touched_points(Device* device, uint16_t* x, uint16_t* y, uint16_t* strength, uint8_t* point_count, uint8_t max_point_count) {
    auto* internal = static_cast<Xpt2046Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_get_coordinates(internal->touch_handle, x, y, strength, point_count, max_point_count);
}

static error_t xpt2046_set_swap_xy(Device* device, bool swap) {
    auto* internal = static_cast<Xpt2046Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_set_swap_xy(internal->touch_handle, swap) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t xpt2046_get_swap_xy(Device* device, bool* swap) {
    auto* internal = static_cast<Xpt2046Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_get_swap_xy(internal->touch_handle, swap) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t xpt2046_set_mirror_x(Device* device, bool mirror) {
    auto* internal = static_cast<Xpt2046Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_set_mirror_x(internal->touch_handle, mirror) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t xpt2046_get_mirror_x(Device* device, bool* mirror) {
    auto* internal = static_cast<Xpt2046Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_get_mirror_x(internal->touch_handle, mirror) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t xpt2046_set_mirror_y(Device* device, bool mirror) {
    auto* internal = static_cast<Xpt2046Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_set_mirror_y(internal->touch_handle, mirror) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t xpt2046_get_mirror_y(Device* device, bool* mirror) {
    auto* internal = static_cast<Xpt2046Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_get_mirror_y(internal->touch_handle, mirror) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

// endregion

static const PointerApi xpt2046_pointer_api = {
    .enter_sleep = xpt2046_enter_sleep,
    .exit_sleep = xpt2046_exit_sleep,
    .read_data = xpt2046_read_data,
    .get_touched_points = xpt2046_get_touched_points,
    .set_swap_xy = xpt2046_set_swap_xy,
    .get_swap_xy = xpt2046_get_swap_xy,
    .set_mirror_x = xpt2046_set_mirror_x,
    .get_mirror_x = xpt2046_get_mirror_x,
    .set_mirror_y = xpt2046_set_mirror_y,
    .get_mirror_y = xpt2046_get_mirror_y,
};

Driver xpt2046_driver = {
    .name = "xpt2046",
    .compatible = (const char*[]) { "xptek,xpt2046", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &xpt2046_pointer_api,
    .device_type = &POINTER_TYPE,
    .owner = &xpt2046_module,
    .internal = nullptr
};
