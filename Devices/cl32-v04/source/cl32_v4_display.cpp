// SPDX-License-Identifier: Apache-2.0
#include "cl32_v4_display.h"

#include <drivers/ssd16xx.h>
#include <drivers/st7305.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/device_listener.h>
#include <tactility/drivers/esp32_spi.h>
#include <tactility/drivers/spi_controller.h>
#include <tactility/log.h>

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_private/esp_gpio_reserve.h>

#include <cstring>

constexpr auto* TAG = "cl32-v4-display";

constexpr gpio_num_t PIN_CS = GPIO_NUM_6;
constexpr gpio_num_t PIN_RESET = GPIO_NUM_12;
constexpr gpio_num_t PIN_DC = GPIO_NUM_13;
constexpr gpio_num_t PIN_BUSY = GPIO_NUM_14;
constexpr uint32_t DISPLAY_CS_INDEX = 1;

static St7305Config st7305_config {};
static Ssd16xxConfig ssd16xx_config {};
static Device display_device {};
static bool display_created = false;

// The probe-once latch for on_spi1_started(). File-scope so the teardown can reset it for a later start/probe cycle.
static bool did_probe = false;

static void send_command(spi_device_handle_t spi, uint8_t command) {
    spi_transaction_t transaction = {};
    transaction.flags = SPI_TRANS_USE_TXDATA;
    transaction.length = 8;
    transaction.tx_data[0] = command;
    gpio_set_level(PIN_CS, 0);
    gpio_set_level(PIN_DC, 0);
    spi_device_polling_transmit(spi, &transaction);
    gpio_set_level(PIN_CS, 1);
}

static bool wait_for_busy_high(TickType_t timeout) {
    const TickType_t start = xTaskGetTickCount();
    while (xTaskGetTickCount() - start <= timeout) {
        if (gpio_get_level(PIN_BUSY)) {
            return true;
        }
        vTaskDelay(1);
    }
    return false;
}

// Only the SSD16xx drives the BUSY line: it goes high during the software reset.
// The pin is pulled down so it stays low when an ST7305 is connected.
static bool detect_ssd16xx(Device* spi1) {
    const auto* spi_config = static_cast<const Esp32SpiConfig*>(spi1->config);

    spi_device_interface_config_t device_config = {};
    device_config.clock_speed_hz = 4000000;
    device_config.mode = 0;
    device_config.spics_io_num = -1;
    device_config.queue_size = 1;
    spi_device_handle_t spi;
    if (spi_bus_add_device(spi_config->host, &device_config, &spi) != ESP_OK) {
        LOG_E(TAG, "Failed to add SPI device for detection");
        return false;
    }

    gpio_config_t outputs = {
        .pin_bit_mask = (1ULL << PIN_DC) | (1ULL << PIN_RESET),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&outputs);

    gpio_config_t input = {
        .pin_bit_mask = 1ULL << PIN_BUSY,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&input);

    gpio_set_level(PIN_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    spi_controller_lock(spi1);
    send_command(spi, 0x12);
    const bool busy = wait_for_busy_high(pdMS_TO_TICKS(20));
    spi_controller_unlock(spi1);

    spi_bus_remove_device(spi);
    gpio_reset_pin(PIN_BUSY);
    // The display driver configures these pins again
    esp_gpio_revoke((1ULL << PIN_DC) | (1ULL << PIN_RESET));
    return busy;
}

static bool create_display(Device* spi1, bool is_ssd16xx) {
    Device* gpio0 = nullptr;
    if (device_get_by_name("gpio0", &gpio0) != ERROR_NONE) {
        LOG_E(TAG, "gpio0 not found");
        return false;
    }

    const char* compatible;
    void* config;
    if (is_ssd16xx) {
        ssd16xx_config = Ssd16xxConfig {
            .pin_dc = { gpio0, PIN_DC, GPIO_FLAG_NONE },
            .pin_reset = { gpio0, PIN_RESET, GPIO_FLAG_NONE },
            .pin_busy = { gpio0, PIN_BUSY, GPIO_FLAG_NONE },
            .pin_cs = { gpio0, PIN_CS, GPIO_FLAG_NONE },
            .clock_speed_hz = 4000000,
            .panel_type = "gdey029t71h",
            .rotation = 1,
        };
        compatible = "solomon-systech,ssd16xx";
        config = &ssd16xx_config;
    } else {
        Device* backlight = nullptr;
        if (device_get_by_name("back_light", &backlight) != ERROR_NONE) {
            LOG_W(TAG, "back_light not found");
        }
        st7305_config = St7305Config {
            .horizontal_resolution = 384,
            .vertical_resolution = 168,
            .column_offset = 23,
            .mirror_x = true,
            .mirror_y = true,
            .invert_color = false,
            .pixel_clock_hz = 80000000,
            .pin_dc = { gpio0, PIN_DC, GPIO_FLAG_NONE },
            .pin_reset = { gpio0, PIN_RESET, GPIO_FLAG_NONE },
            .backlight = backlight,
        };
        compatible = "sitronix,st7305";
        config = &st7305_config;
    }

    display_device = Device {
        .address = DISPLAY_CS_INDEX,
        .name = "display",
        .config = config,
        .parent = nullptr,
        .flags = 0,
        .internal = nullptr,
    };

    error_t error = device_construct(&display_device);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to construct display: %s", error_to_string(error));
        return false;
    }

    device_set_parent(&display_device, spi1);

    Driver* driver = driver_find_compatible(compatible);
    if (driver == nullptr) {
        LOG_E(TAG, "No driver registered for %s", compatible);
        device_destruct(&display_device);
        return false;
    }
    device_set_driver(&display_device, driver);

    error = device_add(&display_device);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to add display: %s", error_to_string(error));
        device_destruct(&display_device);
        return false;
    }

    error = device_start(&display_device);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to start display: %s", error_to_string(error));
        device_remove(&display_device);
        device_destruct(&display_device);
        return false;
    }

    display_created = true;
    return true;
}

// Fires for every device's start/stop in the system.
static void on_spi1_started(Device* device, DeviceEvent event, void* context) {
    (void)context;

    if (did_probe || event != DEVICE_EVENT_STARTED || strcmp(device->name, "spi1") != 0) {
        return;
    }
    did_probe = true;

    const bool is_ssd16xx = detect_ssd16xx(device);
    LOG_I(TAG, "Detected %s display", is_ssd16xx ? "SSD16xx e-paper" : "ST7305 LCD");
    create_display(device, is_ssd16xx);
}

void cl32_v4_display_detect_start() {
    device_listener_add(on_spi1_started, nullptr);
}

void cl32_v4_display_detect_stop() {
    device_listener_remove(on_spi1_started, nullptr);
}

void cl32_v4_display_teardown() {
    if (display_created) {
        device_stop(&display_device);
        device_remove(&display_device);
        device_destruct(&display_device);
        display_created = false;
    }
    did_probe = false;
}
