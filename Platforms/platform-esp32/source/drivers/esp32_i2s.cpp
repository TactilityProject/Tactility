// SPDX-License-Identifier: Apache-2.0
#include <driver/i2s_std.h>
#include <driver/i2s_common.h>
#include <soc/soc_caps.h>
#ifdef SOC_I2S_SUPPORTS_TDM
#include <driver/i2s_tdm.h>
#endif
#ifdef SOC_I2S_SUPPORTS_PDM_RX
#include <driver/i2s_pdm.h>
#endif

#include <tactility/driver.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/gpio_descriptor.h>
#include <tactility/drivers/i2s_controller.h>
#include <tactility/log.h>

#include <tactility/time.h>
#include <tactility/error_esp32.h>
#include <tactility/drivers/esp32_gpio_helpers.h>
#include <tactility/drivers/esp32_i2s.h>

#include <new>

#define TAG "esp32_i2s"

struct Esp32I2sInternal {
    Mutex mutex {};
    I2sConfig config {};
    bool config_set = false;
#ifdef SOC_I2S_SUPPORTS_TDM
    bool rx_tdm_mode = false;
    I2sTdmRxConfig tdm_config {};
#endif
    GpioDescriptor* bclk_descriptor = nullptr;
    GpioDescriptor* ws_descriptor = nullptr;
    GpioDescriptor* data_out_descriptor = nullptr;
    GpioDescriptor* data_in_descriptor = nullptr;
    GpioDescriptor* mclk_descriptor = nullptr;
    i2s_chan_handle_t tx_handle = nullptr;
    i2s_chan_handle_t rx_handle = nullptr;

    Esp32I2sInternal() {
        mutex_construct(&mutex);
    }

    ~Esp32I2sInternal() {
        cleanup_pins();
        mutex_destruct(&mutex);
    }

    void cleanup_pins() {
        release_pin(&bclk_descriptor);
        release_pin(&ws_descriptor);
        release_pin(&data_out_descriptor);
        release_pin(&data_in_descriptor);
        release_pin(&mclk_descriptor);
    }

    bool init_pins(Esp32I2sConfig* dts_config) {
        check (!ws_descriptor && !bclk_descriptor && !data_out_descriptor && !data_in_descriptor && !mclk_descriptor);
        auto& ws_spec = dts_config->pin_ws;
        auto& bclk_spec = dts_config->pin_bclk;
        auto& data_in_spec = dts_config->pin_data_in;
        auto& data_out_spec = dts_config->pin_data_out;
        auto& mclk_spec = dts_config->pin_mclk;

        bool success = acquire_pin_or_set_null(ws_spec, GPIO_FLAG_DIRECTION_OUTPUT, &ws_descriptor) &&
            acquire_pin_or_set_null(bclk_spec, GPIO_FLAG_DIRECTION_OUTPUT, &bclk_descriptor) &&
            acquire_pin_or_set_null(data_in_spec, GPIO_FLAG_DIRECTION_INPUT, &data_in_descriptor) &&
            acquire_pin_or_set_null(data_out_spec, GPIO_FLAG_DIRECTION_OUTPUT, &data_out_descriptor) &&
            acquire_pin_or_set_null(mclk_spec, GPIO_FLAG_DIRECTION_OUTPUT, &mclk_descriptor);

        if (!success) {
            cleanup_pins();
            LOG_E(TAG, "Failed to acquire all pins");
            return false;
        }

        return true;
    }
};

#define GET_CONFIG(device) ((Esp32I2sConfig*)(device)->config)
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

static void get_esp32_std_config(Esp32I2sInternal* internal, const I2sConfig* config, i2s_std_config_t* std_cfg) {
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

    gpio_num_t mclk_pin = get_native_pin(internal->mclk_descriptor);
    gpio_num_t bclk_pin = get_native_pin(internal->bclk_descriptor);
    gpio_num_t ws_pin = get_native_pin(internal->ws_descriptor);
    gpio_num_t data_out_pin = get_native_pin(internal->data_out_descriptor);
    gpio_num_t data_in_pin = get_native_pin(internal->data_in_descriptor);
    LOG_I(TAG, "Configuring I2S pins: MCLK=%d, BCLK=%d, WS=%d, DATA_OUT=%d, DATA_IN=%d", mclk_pin, bclk_pin, ws_pin, data_out_pin, data_in_pin);

    bool mclk_inverted = is_pin_inverted(internal->mclk_descriptor);
    bool bclk_inverted = is_pin_inverted(internal->bclk_descriptor);
    bool ws_inverted = is_pin_inverted(internal->ws_descriptor);
    LOG_I(TAG, "Inverted pins: MCLK=%u, BCLK=%u, WS=%u", mclk_inverted, bclk_inverted, ws_inverted);

    std_cfg->gpio_cfg = {
        .mclk = mclk_pin,
        .bclk = bclk_pin,
        .ws = ws_pin,
        .dout = data_out_pin,
        .din = data_in_pin,
        .invert_flags = {
            .mclk_inv = mclk_inverted,
            .bclk_inv = bclk_inverted,
            .ws_inv = ws_inverted
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

    auto* internal = GET_DATA(device);
    auto* dts_config = GET_CONFIG(device);

    // Standard mode always drives BCLK -- unlike PDM RX (which uses pin-ws as its clock
    // line), there's no mode where BCLK is legitimately absent here. pin-bclk is optional
    // in the devicetree binding only to support PDM-only controllers; reject a missing
    // pin here rather than silently building an std_cfg with BCLK=-1.
    if (internal->bclk_descriptor == nullptr) {
        LOG_E(TAG, "Standard I2S mode requires pin-bclk");
        return ERROR_INVALID_ARGUMENT;
    }

    lock(internal);

    cleanup_channel_handles(internal);
    internal->config_set = false;
#ifdef SOC_I2S_SUPPORTS_TDM
    internal->rx_tdm_mode = false;
#endif

    // Create new channel handles
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(dts_config->port, I2S_ROLE_MASTER);
    esp_err_t esp_error = i2s_new_channel(&chan_cfg, &internal->tx_handle, &internal->rx_handle);
    if (esp_error != ESP_OK) {
        LOG_E(TAG, "Failed to create I2S channels: %s", esp_err_to_name(esp_error));
        unlock(internal);
        return ERROR_RESOURCE;
    }

    i2s_std_config_t std_cfg = {};
    get_esp32_std_config(internal, config, &std_cfg);

    if (internal->tx_handle) {
        esp_error = i2s_channel_init_std_mode(internal->tx_handle, &std_cfg);
        if (esp_error == ESP_OK) esp_error = i2s_channel_enable(internal->tx_handle);
    }
    if (esp_error == ESP_OK && internal->rx_handle) {
        esp_error = i2s_channel_init_std_mode(internal->rx_handle, &std_cfg);
        if (esp_error == ESP_OK) esp_error = i2s_channel_enable(internal->rx_handle);
    }

    if (esp_error != ESP_OK) {
        LOG_E(TAG, "Failed to initialize/enable I2S channels: %s", esp_err_to_name(esp_error));
        cleanup_channel_handles(internal);
        unlock(internal);
        return esp_err_to_error(esp_error);
    }

    // Update runtime config to reflect current state
    memcpy(&internal->config, config, sizeof(I2sConfig));
    internal->config_set = true;

    unlock(internal);
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

    auto* dts_config = GET_CONFIG(device);

    auto* data = new(std::nothrow) Esp32I2sInternal();
    if (!data) return ERROR_OUT_OF_MEMORY;

    if (!data->init_pins(dts_config)) {
        LOG_E(TAG, "Failed to init one or more pins");
        delete data;
        return ERROR_RESOURCE;
    }

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

static error_t disable_direction(Device* device, bool is_input) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;

    LOG_I(TAG, "disable_direction %s (%s)", device->name, is_input ? "rx" : "tx");
    auto* driver_data = GET_DATA(device);
    lock(driver_data);
    if (is_input) {
        if (driver_data->rx_handle) {
            i2s_channel_disable(driver_data->rx_handle);
            i2s_del_channel(driver_data->rx_handle);
            driver_data->rx_handle = nullptr;
        }
    } else {
        if (driver_data->tx_handle) {
            i2s_channel_disable(driver_data->tx_handle);
            i2s_del_channel(driver_data->tx_handle);
            driver_data->tx_handle = nullptr;
        }
    }
    unlock(driver_data);
    return ERROR_NONE;
}

#ifdef SOC_I2S_SUPPORTS_TDM
static error_t set_rx_tdm_config(Device* device, const struct I2sTdmRxConfig* config) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;

    if (config->bits_per_sample != 8 &&
        config->bits_per_sample != 16 &&
        config->bits_per_sample != 24 &&
        config->bits_per_sample != 32) {
        return ERROR_INVALID_ARGUMENT;
    }

    if (config->slot_count == 0 || config->slot_count > 16) {
        return ERROR_INVALID_ARGUMENT;
    }

    if (config->bclk_div == 0) {
        return ERROR_INVALID_ARGUMENT;
    }

    if (config->sample_rate_hz == 0) {
        return ERROR_INVALID_ARGUMENT;
    }

    uint32_t bytes_per_sample = config->bits_per_sample / 8u;

    // Size DMA buffers so that each descriptor covers at most 4096 bytes.
    // Start at 512 frames and halve until one frame × slot_count × bytes fits.
    uint32_t frame_num = 512u;
    uint32_t dma_desc_num = 8u;
    while (frame_num > 64u && frame_num * (uint32_t)config->slot_count * bytes_per_sample > 4096u) {
        frame_num /= 2u;
    }

    // Reject if even the minimum frame count overflows one descriptor (slot_count too large).
    if (frame_num * (uint32_t)config->slot_count * bytes_per_sample > 4096u) {
        return ERROR_INVALID_ARGUMENT;
    }

    auto* internal = GET_DATA(device);
    auto* dts_config = GET_CONFIG(device);

    // TDM, like standard mode, always drives BCLK -- reject rather than build a tdm_cfg
    // with BCLK=-1 (see set_config() for the matching check/rationale).
    if (internal->bclk_descriptor == nullptr) {
        LOG_E(TAG, "TDM mode requires pin-bclk");
        return ERROR_INVALID_ARGUMENT;
    }

    lock(internal);

    // Tear down only the RX channel; TX stays in standard mode for playback.
    if (internal->rx_handle) {
        i2s_channel_disable(internal->rx_handle);
        i2s_del_channel(internal->rx_handle);
        internal->rx_handle = nullptr;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(dts_config->port, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num  = dma_desc_num;
    chan_cfg.dma_frame_num = (uint32_t)frame_num;
    i2s_chan_handle_t new_rx = nullptr;
    esp_err_t esp_error = i2s_new_channel(&chan_cfg, nullptr, &new_rx);
    if (esp_error != ESP_OK) {
        LOG_E(TAG, "TDM: failed to create RX channel: %s", esp_err_to_name(esp_error));
        // RX channel is gone; caller must call set_config() to restore standard mode.
        unlock(internal);
        return ERROR_RESOURCE;
    }

    gpio_num_t mclk_pin     = get_native_pin(internal->mclk_descriptor);
    gpio_num_t bclk_pin     = get_native_pin(internal->bclk_descriptor);
    gpio_num_t ws_pin       = get_native_pin(internal->ws_descriptor);
    gpio_num_t data_in_pin  = get_native_pin(internal->data_in_descriptor);

    // slot_mask: bits 0..(slot_count-1) set; slot_count validated above (1-16)
    i2s_tdm_slot_mask_t slot_mask = (i2s_tdm_slot_mask_t)((1u << config->slot_count) - 1u);

    i2s_tdm_config_t tdm_cfg = {
        .clk_cfg = {
            .sample_rate_hz  = config->sample_rate_hz,
            .clk_src         = I2S_CLK_SRC_DEFAULT,
            .ext_clk_freq_hz = 0,
            .mclk_multiple   = (i2s_mclk_multiple_t)config->mclk_multiple,
            .bclk_div        = config->bclk_div,
        },
        .slot_cfg = {
            .data_bit_width = to_esp32_bits_per_sample(config->bits_per_sample),
            .slot_bit_width = (config->slot_bit_width == 0) ? I2S_SLOT_BIT_WIDTH_AUTO : (i2s_slot_bit_width_t)config->slot_bit_width,
            .slot_mode      = I2S_SLOT_MODE_STEREO,
            .slot_mask      = slot_mask,
            .ws_width       = I2S_TDM_AUTO_WS_WIDTH,
            .ws_pol         = false,
            .bit_shift      = true,
            .left_align     = false,
            .big_endian     = false,
            .bit_order_lsb  = false,
            .skip_mask      = false,
            .total_slot     = I2S_TDM_AUTO_SLOT_NUM,
        },
        .gpio_cfg = {
            .mclk = mclk_pin,
            .bclk = bclk_pin,
            .ws   = ws_pin,
            .dout = I2S_GPIO_UNUSED,
            .din  = data_in_pin,
            .invert_flags = {
                .mclk_inv = is_pin_inverted(internal->mclk_descriptor),
                .bclk_inv = is_pin_inverted(internal->bclk_descriptor),
                .ws_inv   = is_pin_inverted(internal->ws_descriptor),
            },
        },
    };

    esp_error = i2s_channel_init_tdm_mode(new_rx, &tdm_cfg);
    if (esp_error == ESP_OK) esp_error = i2s_channel_enable(new_rx);

    if (esp_error != ESP_OK) {
        LOG_E(TAG, "TDM: failed to init/enable RX channel: %s", esp_err_to_name(esp_error));
        i2s_del_channel(new_rx);
        // RX channel is gone; caller must call set_config() to restore standard mode.
        unlock(internal);
        return esp_err_to_error(esp_error);
    }

    internal->rx_handle = new_rx;
    internal->rx_tdm_mode = true;
    memcpy(&internal->tdm_config, config, sizeof(I2sTdmRxConfig));
    unlock(internal);
    return ERROR_NONE;
}
#endif // SOC_I2S_SUPPORTS_TDM

#ifdef SOC_I2S_SUPPORTS_PDM_RX
static error_t set_rx_pdm_config(Device* device, const struct I2sPdmRxConfig* config) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;

    if (config->sample_rate_hz == 0) {
        return ERROR_INVALID_ARGUMENT;
    }

    auto* internal = GET_DATA(device);
    auto* dts_config = GET_CONFIG(device);

    // PDM RX is hardware-restricted to I2S controller 0 on ESP32 targets.
    if (dts_config->port != I2S_NUM_0) {
        return ERROR_NOT_SUPPORTED;
    }

    lock(internal);

    // Tear down only the RX channel; TX (if any) stays in its own mode for playback.
    if (internal->rx_handle) {
        i2s_channel_disable(internal->rx_handle);
        i2s_del_channel(internal->rx_handle);
        internal->rx_handle = nullptr;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(dts_config->port, I2S_ROLE_MASTER);
    i2s_chan_handle_t new_rx = nullptr;
    esp_err_t esp_error = i2s_new_channel(&chan_cfg, nullptr, &new_rx);
    if (esp_error != ESP_OK) {
        LOG_E(TAG, "PDM: failed to create RX channel: %s", esp_err_to_name(esp_error));
        unlock(internal);
        return ERROR_RESOURCE;
    }

    gpio_num_t clk_pin    = get_native_pin(internal->ws_descriptor);
    gpio_num_t data_in_pin = get_native_pin(internal->data_in_descriptor);

    i2s_slot_mode_t slot_mode = config->stereo ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO;

    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(config->sample_rate_hz),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, slot_mode),
        .gpio_cfg = {
            .clk = clk_pin,
            .din = data_in_pin,
            .invert_flags = {
                .clk_inv = is_pin_inverted(internal->ws_descriptor),
            },
        },
    };

    esp_error = i2s_channel_init_pdm_rx_mode(new_rx, &pdm_cfg);
    if (esp_error == ESP_OK) esp_error = i2s_channel_enable(new_rx);

    if (esp_error != ESP_OK) {
        LOG_E(TAG, "PDM: failed to init/enable RX channel: %s", esp_err_to_name(esp_error));
        i2s_del_channel(new_rx);
        unlock(internal);
        return esp_err_to_error(esp_error);
    }

    internal->rx_handle = new_rx;
    unlock(internal);
    return ERROR_NONE;
}
#endif // SOC_I2S_SUPPORTS_PDM_RX

const static I2sControllerApi esp32_i2s_api = {
    .read = read,
    .write = write,
    .set_config = set_config,
    .get_config = get_config,
    .reset = reset,
#ifdef SOC_I2S_SUPPORTS_TDM
    .set_rx_tdm_config = set_rx_tdm_config,
#else
    .set_rx_tdm_config = nullptr,
#endif
#ifdef SOC_I2S_SUPPORTS_PDM_RX
    .set_rx_pdm_config = set_rx_pdm_config,
#else
    .set_rx_pdm_config = nullptr,
#endif
    .disable_direction = disable_direction,
};

extern struct Module platform_esp32_module;

Driver esp32_i2s_driver = {
    .name = "esp32_i2s",
    .compatible = (const char*[]) { "espressif,esp32-i2s", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = (void*)&esp32_i2s_api,
    .device_type = &I2S_CONTROLLER_TYPE,
    .owner = &platform_esp32_module,
    .internal = nullptr
};

} // extern "C"
