// SPDX-License-Identifier: Apache-2.0
#include <driver/i2s_std.h>
#include <driver/i2s_common.h>

#include <tactility/driver.h>
#include <tactility/drivers/i2s_controller.h>
#include <tactility/log.h>

#include <tactility/time.h>
#include <tactility/error_esp32.h>
#include <tactility/drivers/esp32_i2s.h>

#include <new>

#define TAG "esp32_i2s"

struct Esp32I2sInternal {
    Mutex mutex {};
    i2s_chan_handle_t tx_handle = nullptr;
    i2s_chan_handle_t rx_handle = nullptr;
    I2sConfig config {};
    bool config_set = false;

    Esp32I2sInternal() {
        mutex_construct(&mutex);
    }

    ~Esp32I2sInternal() {
        mutex_destruct(&mutex);
    }
};

#define GET_CONFIG(device) ((Esp32I2sConfig*)device->config)
#define GET_DATA(device) ((Esp32I2sInternal*)device_get_driver_data(device))

#define lock(data) mutex_lock(&data->mutex);
#define unlock(data) mutex_unlock(&data->mutex);

extern "C" {

static error_t cleanup_channel_handles(Esp32I2sInternal* driver_data) {
    // TODO: error handling of i2ss functions
    if (driver_data->tx_handle) {
        i2s_channel_disable(driver_data->tx_handle);
        i2s_del_channel(driver_data->tx_handle);
        driver_data->tx_handle = nullptr;
    }
    if (driver_data->rx_handle) {
        i2s_channel_disable(driver_data->rx_handle);
        i2s_del_channel(driver_data->rx_handle);
        driver_data->rx_handle = nullptr;
    }
    return ERROR_NONE;
}

static i2s_data_bit_width_t to_esp32_bits_per_sample(uint8_t bits) {
    switch (bits) {
        case 8: return I2S_DATA_BIT_WIDTH_8BIT;
        case 16: return I2S_DATA_BIT_WIDTH_16BIT;
        case 24: return I2S_DATA_BIT_WIDTH_24BIT;
        case 32: return I2S_DATA_BIT_WIDTH_32BIT;
        default: return I2S_DATA_BIT_WIDTH_16BIT;
    }
}

static void get_esp32_std_config(const I2sConfig* config, const Esp32I2sConfig* dts_config, i2s_std_config_t* std_cfg) {
    std_cfg->clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(config->sample_rate);
    std_cfg->slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(to_esp32_bits_per_sample(config->bits_per_sample), I2S_SLOT_MODE_STEREO);
    
    if (config->communication_format & I2S_FORMAT_STAND_MSB) {
        std_cfg->slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(to_esp32_bits_per_sample(config->bits_per_sample), I2S_SLOT_MODE_STEREO);
    } else if (config->communication_format & (I2S_FORMAT_STAND_PCM_SHORT | I2S_FORMAT_STAND_PCM_LONG)) {
        std_cfg->slot_cfg = I2S_STD_PCM_SLOT_DEFAULT_CONFIG(to_esp32_bits_per_sample(config->bits_per_sample), I2S_SLOT_MODE_STEREO);
    }

    if (config->channel_left != I2S_CHANNEL_NONE && config->channel_right == I2S_CHANNEL_NONE) {
        std_cfg->slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    } else if (config->channel_left == I2S_CHANNEL_NONE && config->channel_right != I2S_CHANNEL_NONE) {
        std_cfg->slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
    } else {
        std_cfg->slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    }

    std_cfg->gpio_cfg = {
        .mclk = (gpio_num_t)dts_config->pin_mclk,
        .bclk = (gpio_num_t)dts_config->pin_bclk,
        .ws = (gpio_num_t)dts_config->pin_ws,
        .dout = (gpio_num_t)dts_config->pin_data_out,
        .din = (gpio_num_t)dts_config->pin_data_in,
        .invert_flags = {
            .mclk_inv = false,
            .bclk_inv = false,
            .ws_inv = false
        }
    };
}

static error_t read(Device* device, void* data, size_t data_size, size_t* bytes_read, TickType_t timeout) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;
    auto* driver_data = GET_DATA(device);
    if (!driver_data->rx_handle) return ERROR_NOT_SUPPORTED;
    
    lock(driver_data);
    const esp_err_t esp_error = i2s_channel_read(driver_data->rx_handle, data, data_size, bytes_read, timeout);
    unlock(driver_data);
    return esp_err_to_error(esp_error);
}

static error_t write(Device* device, const void* data, size_t data_size, size_t* bytes_written, TickType_t timeout) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;
    auto* driver_data = GET_DATA(device);
    if (!driver_data->tx_handle) return ERROR_NOT_SUPPORTED;

    lock(driver_data);
    const esp_err_t esp_error = i2s_channel_write(driver_data->tx_handle, data, data_size, bytes_written, timeout);
    unlock(driver_data);
    return esp_err_to_error(esp_error);
}

static error_t set_config(Device* device, const struct I2sConfig* config) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;

    if (
        config->bits_per_sample != 8 &&
        config->bits_per_sample != 16 &&
        config->bits_per_sample != 24 &&
        config->bits_per_sample != 32
    ) {
        return ERROR_INVALID_ARGUMENT;
    }

    auto* driver_data = GET_DATA(device);
    auto* dts_config = GET_CONFIG(device);
    lock(driver_data);

    cleanup_channel_handles(driver_data);
    driver_data->config_set = false;

    // Create new channel handles
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(dts_config->port, I2S_ROLE_MASTER);
    esp_err_t esp_error = i2s_new_channel(&chan_cfg, &driver_data->tx_handle, &driver_data->rx_handle);
    if (esp_error != ESP_OK) {
        LOG_E(TAG, "Failed to create I2S channels: %s", esp_err_to_name(esp_error));
        unlock(driver_data);
        return ERROR_RESOURCE;
    }

    i2s_std_config_t std_cfg;
    get_esp32_std_config(config, dts_config, &std_cfg);

    if (driver_data->tx_handle) {
        esp_error = i2s_channel_init_std_mode(driver_data->tx_handle, &std_cfg);
        if (esp_error == ESP_OK) esp_error = i2s_channel_enable(driver_data->tx_handle);
    }
    if (esp_error == ESP_OK && driver_data->rx_handle) {
        esp_error = i2s_channel_init_std_mode(driver_data->rx_handle, &std_cfg);
        if (esp_error == ESP_OK) esp_error = i2s_channel_enable(driver_data->rx_handle);
    }

    if (esp_error != ESP_OK) {
        LOG_E(TAG, "Failed to initialize/enable I2S channels: %s", esp_err_to_name(esp_error));
        cleanup_channel_handles(driver_data);
        unlock(driver_data);
        return esp_err_to_error(esp_error);
    }

    // Update runtime config to reflect current state
    memcpy(&driver_data->config, config, sizeof(I2sConfig));
    driver_data->config_set = true;

    unlock(driver_data);
    return ERROR_NONE;
}

static error_t get_config(Device* device, struct I2sConfig* config) {
    auto* driver_data = GET_DATA(device);

    lock(driver_data);
    if (!driver_data->config_set) {
        unlock(driver_data);
        return ERROR_RESOURCE;
    }
    memcpy(config, &driver_data->config, sizeof(I2sConfig));
    unlock(driver_data);

    return ERROR_NONE;
}

static error_t start(Device* device) {
    ESP_LOGI(TAG, "start %s", device->name);
    auto* data = new(std::nothrow) Esp32I2sInternal();
    if (!data) return ERROR_OUT_OF_MEMORY;

    device_set_driver_data(device, data);

    return ERROR_NONE;
}

static error_t stop(Device* device) {
    ESP_LOGI(TAG, "stop %s", device->name);
    auto* driver_data = GET_DATA(device);

    lock(driver_data);
    cleanup_channel_handles(driver_data);
    unlock(driver_data);

    device_set_driver_data(device, nullptr);
    delete driver_data;
    return ERROR_NONE;
}

static error_t reset(Device* device) {
    ESP_LOGI(TAG, "reset %s", device->name);
    auto* driver_data = GET_DATA(device);
    lock(driver_data);
    cleanup_channel_handles(driver_data);
    unlock(driver_data);
    return ERROR_NONE;
}

const static I2sControllerApi esp32_i2s_api = {
    .read = read,
    .write = write,
    .set_config = set_config,
    .get_config = get_config,
    .reset = reset
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
    .internal = nullptr
};

} // extern "C"
