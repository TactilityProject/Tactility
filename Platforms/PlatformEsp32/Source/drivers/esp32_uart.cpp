// SPDX-License-Identifier: Apache-2.0
#include <driver/uart.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/uart_controller.h>
#include <tactility/drivers/esp32_uart.h>
#include <tactility/error_esp32.h>
#include <tactility/log.h>
#include <tactility/time.h>

#include <new>

#define TAG "esp32_uart"

struct Esp32UartInternal {
    Mutex mutex {};
    UartConfig config {};
    bool config_set = false;
    bool is_open = false;

    Esp32UartInternal() {
        mutex_construct(&mutex);
    }

    ~Esp32UartInternal() {
        mutex_destruct(&mutex);
    }
};

#define GET_CONFIG(device) ((Esp32UartConfig*)device->config)
#define GET_DATA(device) ((Esp32UartInternal*)device_get_driver_data(device))

#define lock(data) mutex_lock(&data->mutex)
#define unlock(data) mutex_unlock(&data->mutex)

extern "C" {

static uart_parity_t to_esp32_parity(enum UartParity parity) {
    switch (parity) {
        case UART_CONTROLLER_PARITY_DISABLE: return UART_PARITY_DISABLE;
        case UART_CONTROLLER_PARITY_EVEN: return UART_PARITY_EVEN;
        case UART_CONTROLLER_PARITY_ODD: return UART_PARITY_ODD;
        default: return UART_PARITY_DISABLE;
    }
}

static uart_word_length_t to_esp32_data_bits(enum UartDataBits bits) {
    switch (bits) {
        case UART_CONTROLLER_DATA_5_BITS: return UART_DATA_5_BITS;
        case UART_CONTROLLER_DATA_6_BITS: return UART_DATA_6_BITS;
        case UART_CONTROLLER_DATA_7_BITS: return UART_DATA_7_BITS;
        case UART_CONTROLLER_DATA_8_BITS: return UART_DATA_8_BITS;
        default: return UART_DATA_8_BITS;
    }
}

static uart_stop_bits_t to_esp32_stop_bits(enum UartStopBits bits) {
    switch (bits) {
        case UART_CONTROLLER_STOP_BITS_1: return UART_STOP_BITS_1;
        case UART_CONTROLLER_STOP_BITS_1_5: return UART_STOP_BITS_1_5;
        case UART_CONTROLLER_STOP_BITS_2: return UART_STOP_BITS_2;
        default: return UART_STOP_BITS_1;
    }
}

static error_t read_byte(Device* device, uint8_t* out, TickType_t timeout) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;
    auto* driver_data = GET_DATA(device);
    auto* dts_config = GET_CONFIG(device);

    lock(driver_data);
    if (!driver_data->is_open) {
        unlock(driver_data);
        return ERROR_INVALID_STATE;
    }

    int len = uart_read_bytes(dts_config->port, out, 1, timeout);
    unlock(driver_data);

    if (len < 0) return ERROR_RESOURCE;
    if (len == 0) return ERROR_TIMEOUT;
    return ERROR_NONE;
}

static error_t write_byte(Device* device, uint8_t out, TickType_t timeout) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;
    auto* driver_data = GET_DATA(device);
    auto* dts_config = GET_CONFIG(device);

    lock(driver_data);
    if (!driver_data->is_open) {
        unlock(driver_data);
        return ERROR_INVALID_STATE;
    }

    int len = uart_write_bytes(dts_config->port, (const char*)&out, 1);
    if (len < 0) {
        unlock(driver_data);
        return ERROR_RESOURCE;
    }
    
    // uart_write_bytes is non-blocking on the buffer but we might want to wait for it to be sent?
    // The API signature has timeout, but ESP-IDF's uart_write_bytes doesn't use it for blocking.
    // However, if we want to ensure it's SENT, we could use uart_wait_tx_done.
    if (timeout > 0) {
        esp_err_t err = uart_wait_tx_done(dts_config->port, timeout);
        unlock(driver_data);
        if (err == ESP_ERR_TIMEOUT) return ERROR_TIMEOUT;
        if (err != ESP_OK) return ERROR_RESOURCE;
    } else {
        unlock(driver_data);
    }

    return ERROR_NONE;
}

static error_t write_bytes(Device* device, const uint8_t* buffer, size_t buffer_size, TickType_t timeout) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;
    auto* driver_data = GET_DATA(device);
    auto* dts_config = GET_CONFIG(device);

    lock(driver_data);
    if (!driver_data->is_open) {
        unlock(driver_data);
        return ERROR_INVALID_STATE;
    }

    int len = uart_write_bytes(dts_config->port, (const char*)buffer, buffer_size);
    if (len < 0) {
        unlock(driver_data);
        return ERROR_RESOURCE;
    }

    if (timeout > 0) {
        esp_err_t err = uart_wait_tx_done(dts_config->port, timeout);
        unlock(driver_data);
        if (err == ESP_ERR_TIMEOUT) return ERROR_TIMEOUT;
        if (err != ESP_OK) return ERROR_RESOURCE;
    } else {
        unlock(driver_data);
    }

    return ERROR_NONE;
}

static error_t read_bytes(Device* device, uint8_t* buffer, size_t buffer_size, TickType_t timeout) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;
    auto* driver_data = GET_DATA(device);
    auto* dts_config = GET_CONFIG(device);

    lock(driver_data);
    if (!driver_data->is_open) {
        unlock(driver_data);
        return ERROR_INVALID_STATE;
    }

    int len = uart_read_bytes(dts_config->port, buffer, buffer_size, timeout);
    unlock(driver_data);

    if (len < 0) return ERROR_RESOURCE;
    if (len < (int)buffer_size) return ERROR_TIMEOUT;

    return ERROR_NONE;
}

static error_t get_available(Device* device, size_t* available_bytes) {
    auto* driver_data = GET_DATA(device);
    auto* dts_config = GET_CONFIG(device);

    lock(driver_data);
    if (!driver_data->is_open) {
        unlock(driver_data);
        return ERROR_INVALID_STATE;
    }

    esp_err_t err = uart_get_buffered_data_len(dts_config->port, available_bytes);
    unlock(driver_data);

    if (err != ESP_OK) return esp_err_to_error(err);
    return ERROR_NONE;
}

static error_t set_config(Device* device, const struct UartConfig* config) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;

    auto* driver_data = GET_DATA(device);
    auto* dts_config = GET_CONFIG(device);
    lock(driver_data);

    if (driver_data->is_open) {
        unlock(driver_data);
        return ERROR_INVALID_STATE;
    }

    uart_config_t uart_cfg = {
        .baud_rate = (int)config->baud_rate,
        .data_bits = to_esp32_data_bits(config->data_bits),
        .parity = to_esp32_parity(config->parity),
        .stop_bits = to_esp32_stop_bits(config->stop_bits),
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {
            .allow_pd = 0,
            .backup_before_sleep = 0
        }
    };

    esp_err_t esp_error = uart_param_config(dts_config->port, &uart_cfg);
    if (esp_error == ESP_OK) {
        esp_error = uart_set_pin(dts_config->port, dts_config->pinTx, dts_config->pinRx, dts_config->pinCts, dts_config->pinRts);
    }

    if (esp_error != ESP_OK) {
        LOG_E(TAG, "Failed to configure UART: %s", esp_err_to_name(esp_error));
        unlock(driver_data);
        return esp_err_to_error(esp_error);
    }

    memcpy(&driver_data->config, config, sizeof(UartConfig));
    driver_data->config_set = true;

    unlock(driver_data);
    return ERROR_NONE;
}

static error_t get_config(Device* device, struct UartConfig* config) {
    auto* driver_data = GET_DATA(device);

    lock(driver_data);
    if (!driver_data->config_set) {
        unlock(driver_data);
        return ERROR_RESOURCE;
    }
    memcpy(config, &driver_data->config, sizeof(UartConfig));
    unlock(driver_data);

    return ERROR_NONE;
}

static error_t open(Device* device) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;
    auto* driver_data = GET_DATA(device);
    auto* dts_config = GET_CONFIG(device);

    lock(driver_data);
    if (driver_data->is_open) {
        unlock(driver_data);
        LOG_W(TAG, "Already open");
        return ERROR_INVALID_STATE;
    }

    if (!driver_data->config_set) {
        unlock(driver_data);
        LOG_E(TAG, "open failed: config not set");
        return ERROR_INVALID_STATE;
    }

    esp_err_t esp_error = uart_driver_install(dts_config->port, 1024, 0, 0, NULL, 0);
    if (esp_error != ESP_OK) {
        LOG_E(TAG, "Failed to install UART driver: %s", esp_err_to_name(esp_error));
        unlock(driver_data);
        return esp_err_to_error(esp_error);
    }

    driver_data->is_open = true;

    unlock(driver_data);
    return ERROR_NONE;
}

static error_t close(Device* device) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;
    auto* driver_data = GET_DATA(device);
    auto* dts_config = GET_CONFIG(device);

    lock(driver_data);
    if (!driver_data->is_open) {
        unlock(driver_data);
        LOG_W(TAG, "Already closed");
        return ERROR_INVALID_STATE;
    }
    uart_driver_delete(dts_config->port);
    driver_data->is_open = false;
    unlock(driver_data);

    return ERROR_NONE;
}

static bool is_open(Device* device) {
    auto* driver_data = GET_DATA(device);
    lock(driver_data);
    bool status = driver_data->is_open;
    unlock(driver_data);
    return status;
}

static error_t flush_input(Device* device) {
    auto* driver_data = GET_DATA(device);
    auto* dts_config = GET_CONFIG(device);

    lock(driver_data);
    if (!driver_data->is_open) {
        unlock(driver_data);
        return ERROR_INVALID_STATE;
    }

    esp_err_t err = uart_flush_input(dts_config->port);
    unlock(driver_data);

    return esp_err_to_error(err);
}

static error_t start(Device* device) {
    ESP_LOGI(TAG, "start %s", device->name);
    auto* data = new(std::nothrow) Esp32UartInternal();
    if (!data) return ERROR_OUT_OF_MEMORY;

    device_set_driver_data(device, data);

    return ERROR_NONE;
}

static error_t stop(Device* device) {
    ESP_LOGI(TAG, "stop %s", device->name);
    auto* driver_data = GET_DATA(device);
    auto* dts_config = GET_CONFIG(device);

    lock(driver_data);
    if (driver_data->is_open) {
        unlock(driver_data);
        return ERROR_INVALID_STATE;
    }
    device_set_driver_data(device, nullptr);
    unlock(driver_data);

    delete driver_data;
    return ERROR_NONE;
}

const static UartControllerApi esp32_uart_api = {
    .read_byte = read_byte,
    .write_byte = write_byte,
    .write_bytes = write_bytes,
    .read_bytes = read_bytes,
    .get_available = get_available,
    .set_config = set_config,
    .get_config = get_config,
    .open = open,
    .close = close,
    .is_open = is_open,
    .flush_input = flush_input
};

extern struct Module platform_module;

Driver esp32_uart_driver = {
    .name = "esp32_uart",
    .compatible = (const char*[]) { "espressif,esp32-uart", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = (void*)&esp32_uart_api,
    .device_type = &UART_CONTROLLER_TYPE,
    .owner = &platform_module,
    .internal = nullptr
};

} // extern "C"
