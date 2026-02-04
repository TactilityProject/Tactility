// SPDX-License-Identifier: Apache-2.0
#include <driver/i2s.h>

#include <tactility/driver.h>
#include <tactility/drivers/i2s_controller.h>
#include <tactility/log.h>

#include <tactility/time.h>
#include <tactility/error_esp32.h>
#include <tactility/drivers/esp32_i2s.h>

#define TAG "esp32_i2s"

struct InternalData {
    Mutex mutex { 0 };

    InternalData() {
        mutex_construct(&mutex);
    }

    ~InternalData() {
        mutex_destruct(&mutex);
    }
};

#define GET_CONFIG(device) ((Esp32I2sConfig*)device->config)
#define GET_DATA(device) ((InternalData*)device->internal.driver_data)

#define lock(data) mutex_lock(&data->mutex);
#define unlock(data) mutex_unlock(&data->mutex);

extern "C" {

static error_t read(Device* device, void* data, size_t data_size, size_t* bytes_read, TickType_t timeout) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;
    auto* driver_data = GET_DATA(device);
    lock(driver_data);
    const esp_err_t esp_error = i2s_read(GET_CONFIG(device)->port, data, data_size, bytes_read, timeout);
    unlock(driver_data);
    return esp_err_to_error(esp_error);
}

static error_t write(Device* device, const void* data, size_t data_size, size_t* bytes_written, TickType_t timeout) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;
    auto* driver_data = GET_DATA(device);
    lock(driver_data);
    const esp_err_t esp_error = i2s_write(GET_CONFIG(device)->port, data, data_size, bytes_written, timeout);
    unlock(driver_data);
    return esp_err_to_error(esp_error);
}

error_t esp32_i2s_get_port(struct Device* device, i2s_port_t* port) {
    auto* config = GET_CONFIG(device);
    *port = config->port;
    return ERROR_NONE;
}

static error_t set_config(Device* device, const struct I2sConfig* config) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;
    auto* driver_data = GET_DATA(device);
    auto* dts_config = GET_CONFIG(device);
    lock(driver_data);

    esp_err_t esp_error = i2s_set_sample_rates(dts_config->port, config->sampleRate);
    if (esp_error != ESP_OK) {
        unlock(driver_data);
        return esp_err_to_error(esp_error);
    }

    esp_error = i2s_set_clk(dts_config->port, config->sampleRate, (i2s_bits_per_sample_t)config->bitsPerSample, (i2s_channel_fmt_t)config->channelFormat);
    if (esp_error != ESP_OK) {
        unlock(driver_data);
        return esp_err_to_error(esp_error);
    }

    // Update dts_config to reflect current state
    dts_config->sampleRate = config->sampleRate;
    dts_config->bitsPerSample = config->bitsPerSample;
    dts_config->channelFormat = config->channelFormat;
    dts_config->communicationFormat = config->communicationFormat;

    unlock(driver_data);
    return ERROR_NONE;
}

static error_t get_config(Device* device, struct I2sConfig* config) {
    auto* dts_config = GET_CONFIG(device);
    config->mode = dts_config->mode;
    config->sampleRate = dts_config->sampleRate;
    config->bitsPerSample = dts_config->bitsPerSample;
    config->channelFormat = dts_config->channelFormat;
    config->communicationFormat = dts_config->communicationFormat;
    return ERROR_NONE;
}

static error_t start(Device* device) {
    ESP_LOGI(TAG, "start %s", device->name);
    auto dts_config = GET_CONFIG(device);

    i2s_config_t esp_config = {
        .mode = (i2s_mode_t)(I2S_MODE_TX | I2S_MODE_RX | (dts_config->mode == I2S_MODE_MASTER ? I2S_MODE_MASTER : I2S_MODE_SLAVE)),
        .sample_rate = dts_config->sampleRate,
        .bits_per_sample = (i2s_bits_per_sample_t)dts_config->bitsPerSample,
        .channel_format = (i2s_channel_fmt_t)dts_config->channelFormat,
        .communication_format = (i2s_comm_format_t)dts_config->communicationFormat,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 64,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    esp_err_t error = i2s_driver_install(dts_config->port, &esp_config, 0, NULL);
    if (error != ESP_OK) {
        LOG_E(TAG, "Failed to install driver at port %d: %s", static_cast<int>(dts_config->port), esp_err_to_name(error));
        return ERROR_RESOURCE;
    }

    i2s_pin_config_t pin_config = {
        .bck_io_num = dts_config->pinBclk,
        .ws_io_num = dts_config->pinWs,
        .data_out_num = dts_config->pinDataOut,
        .data_in_num = dts_config->pinDataIn
    };

    error = i2s_set_pin(dts_config->port, &pin_config);
    if (error != ESP_OK) {
        LOG_E(TAG, "Failed to set pins for port %d: %s", static_cast<int>(dts_config->port), esp_err_to_name(error));
        i2s_driver_uninstall(dts_config->port);
        return ERROR_RESOURCE;
    }

    auto* data = new InternalData();
    device_set_driver_data(device, data);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    ESP_LOGI(TAG, "stop %s", device->name);
    auto* driver_data = static_cast<InternalData*>(device_get_driver_data(device));

    i2s_port_t port = GET_CONFIG(device)->port;
    esp_err_t result = i2s_driver_uninstall(port);
    if (result != ESP_OK) {
        LOG_E(TAG, "Failed to uninstall driver at port %d: %s", static_cast<int>(port), esp_err_to_name(result));
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, nullptr);
    delete driver_data;
    return ERROR_NONE;
}

const static I2sControllerApi esp32_i2s_api = {
    .read = read,
    .write = write,
    .set_config = set_config,
    .get_config = get_config
};

extern struct Module platform_module;

Driver esp32_i2s_driver = {
    .name = "esp32_i2s",
    .compatible = (const char*[]) { "espressif,esp32-i2s", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = (void*)&esp32_i2s_api,
    .device_type = &I2S_CONTROLLER_TYPE,
    .owner = &platform_module,
    .driver_private = nullptr
};

} // extern "C"
