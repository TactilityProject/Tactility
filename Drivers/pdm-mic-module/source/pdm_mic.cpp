// SPDX-License-Identifier: Apache-2.0
#include <drivers/pdm_mic.h>

#include <tactility/device.h>
#include <tactility/drivers/audio_codec.h>
#include <tactility/drivers/audio_codec_adapters.h>
#include <tactility/drivers/i2s_controller.h>
#include <tactility/log.h>

#include <dummy_codec.h>
#include <esp_codec_dev.h>

#define TAG "PdmMic"

namespace {

// PDM mics have no fixed native rate of their own -- the PDM RX peripheral generates
// whatever clock the configured sample rate implies. Report a common voice-capture
// default; audio_stream resamples to whatever rate it is opened with.
constexpr uint32_t NATIVE_SAMPLE_RATE = 16000;

struct PdmMicData {
    const audio_codec_data_if_t* data_if = nullptr;
    const audio_codec_if_t* codec_if = nullptr;
    esp_codec_dev_handle_t codec_device = nullptr;
    uint8_t native_channels = 1;
    float input_gain = 1.0f;
    bool is_open = false;
    esp_codec_dev_sample_info_t open_sample_info = {};
};

#define GET_CONFIG(device) (static_cast<const PdmMicConfig*>((device)->config))
#define GET_DATA(device) (static_cast<PdmMicData*>(device_get_driver_data(device)))

// region AudioCodecApi

error_t open(Device* device, const struct AudioCodecStreamConfig* config) {
    auto* data = GET_DATA(device);
    if (data->codec_device == nullptr) {
        return ERROR_RESOURCE;
    }

    // Input-only codec (see get_capabilities()/write()) -- BOTH would imply output support
    // that write() can't actually provide.
    if (config->direction != AUDIO_CODEC_DIR_INPUT) {
        return ERROR_NOT_SUPPORTED;
    }

    esp_codec_dev_sample_info_t sample_info = {
        .bits_per_sample = config->bits_per_sample,
        .channel = config->channels,
        .channel_mask = 0,
        .sample_rate = config->sample_rate,
        .mclk_multiple = 0,
    };

    if (data->is_open) {
        bool same_config = data->open_sample_info.bits_per_sample == sample_info.bits_per_sample
            && data->open_sample_info.channel == sample_info.channel
            && data->open_sample_info.sample_rate == sample_info.sample_rate;
        return same_config ? ERROR_NONE : ERROR_RESOURCE;
    }

    if (esp_codec_dev_open(data->codec_device, &sample_info) != ESP_CODEC_DEV_OK) {
        LOG_E(TAG, "Failed to open codec device");
        return ERROR_RESOURCE;
    }

    data->is_open = true;
    data->open_sample_info = sample_info;
    return ERROR_NONE;
}

error_t close(Device* device) {
    auto* data = GET_DATA(device);
    if (data->codec_device == nullptr) {
        return ERROR_RESOURCE;
    }

    if (data->is_open) {
        esp_codec_dev_close(data->codec_device);
        data->is_open = false;
    }

    return ERROR_NONE;
}

error_t read(Device* device, void* buffer, size_t size, size_t* bytes_read, TickType_t timeout) {
    (void) timeout;
    auto* data = GET_DATA(device);
    if (!data->is_open) {
        return ERROR_RESOURCE;
    }

    // esp_codec_dev_read returns the number of bytes read (>= 0) on success, or a negative
    // ESP_CODEC_DEV_* error code on failure -- it does NOT return ESP_CODEC_DEV_OK (0) for
    // a successful nonzero-length read.
    int result = esp_codec_dev_read(data->codec_device, buffer, (int) size);
    if (result < 0) {
        return ERROR_RESOURCE;
    }

    *bytes_read = (size_t) result;
    return ERROR_NONE;
}

error_t write(Device* device, const void* buffer, size_t size, size_t* bytes_written, TickType_t timeout) {
    (void) device;
    (void) buffer;
    (void) size;
    (void) bytes_written;
    (void) timeout;
    return ERROR_NOT_SUPPORTED;
}

// PDM mics have no hardware gain/mute registers, and unlike esp_codec_dev's output path
// (esp_codec_dev_set_out_vol(), which falls back to a software volume handler when the
// codec_if has no set_vol), the input path (esp_codec_dev_set_in_gain()/set_in_mute()) has
// no such fallback -- it returns ESP_CODEC_DEV_NOT_SUPPORT outright when the codec_if's
// set_mic_gain/mute_mic are NULL, which dummy_codec always leaves NULL. So input
// volume/mute genuinely isn't supported through this codec for now; report that honestly
// rather than masking a dead call path.

error_t set_volume(Device* device, AudioCodecDirection direction, float volume_percent) {
    (void) device;
    (void) volume_percent;
    if (direction != AUDIO_CODEC_DIR_INPUT) {
        return ERROR_NOT_SUPPORTED;
    }
    return ERROR_NOT_SUPPORTED;
}

error_t get_volume(Device* device, AudioCodecDirection direction, float* volume_percent) {
    (void) device;
    (void) volume_percent;
    if (direction != AUDIO_CODEC_DIR_INPUT) {
        return ERROR_NOT_SUPPORTED;
    }
    return ERROR_NOT_SUPPORTED;
}

error_t set_mute(Device* device, AudioCodecDirection direction, bool muted) {
    (void) device;
    (void) muted;
    if (direction != AUDIO_CODEC_DIR_INPUT) {
        return ERROR_NOT_SUPPORTED;
    }
    return ERROR_NOT_SUPPORTED;
}

error_t get_mute(Device* device, AudioCodecDirection direction, bool* muted) {
    (void) device;
    (void) muted;
    if (direction != AUDIO_CODEC_DIR_INPUT) {
        return ERROR_NOT_SUPPORTED;
    }
    return ERROR_NOT_SUPPORTED;
}

error_t get_native_channels(Device* device, AudioCodecDirection direction, uint8_t* channels) {
    auto* data = GET_DATA(device);
    if (direction != AUDIO_CODEC_DIR_INPUT) {
        return ERROR_NOT_SUPPORTED;
    }
    *channels = data->native_channels;
    return ERROR_NONE;
}

error_t get_native_sample_rate(Device* device, AudioCodecDirection direction, uint32_t* rate_hz) {
    (void) device;
    if (direction != AUDIO_CODEC_DIR_INPUT) {
        return ERROR_NOT_SUPPORTED;
    }
    *rate_hz = NATIVE_SAMPLE_RATE;
    return ERROR_NONE;
}

error_t get_capabilities(Device* device, AudioCodecDirection* supported_directions) {
    (void) device;
    *supported_directions = AUDIO_CODEC_DIR_INPUT;
    return ERROR_NONE;
}

error_t get_input_gain_multiplier(Device* device, float* gain) {
    auto* data = GET_DATA(device);
    *gain = data->input_gain;
    return ERROR_NONE;
}

static const struct AudioCodecApi API = {
    .open = open,
    .close = close,
    .read = read,
    .write = write,
    .set_volume = set_volume,
    .get_volume = get_volume,
    .set_mute = set_mute,
    .get_mute = get_mute,
    .get_native_sample_rate = get_native_sample_rate,
    .get_native_channels = get_native_channels,
    .get_capabilities = get_capabilities,
    .get_input_gain_multiplier = get_input_gain_multiplier,
};

// endregion

// region Driver lifecycle

error_t start_device(Device* device) {
    const auto* config = GET_CONFIG(device);

    if (config->channels == 0 || config->channels > 2) {
        LOG_E(TAG, "Invalid channel count %u (must be 1 or 2)", config->channels);
        return ERROR_RESOURCE;
    }

    // devicetree's int property type has no min/max constraint support, so a bogus
    // negative literal (e.g. "input-gain-percent = <-1>;") would otherwise silently wrap to
    // 65535 when narrowed to uint16_t and produce a wildly wrong gain. Bound it here
    // instead -- 0 disables boost entirely, 2000 (20x) is already an extreme upper bound
    // for compensating a quiet mic capsule.
    if (config->input_gain_percent > 2000) {
        LOG_E(TAG, "Invalid input_gain_percent %u (must be 0..2000)", config->input_gain_percent);
        return ERROR_RESOURCE;
    }

    auto* i2s_controller = config->i2s_device;
    if (i2s_controller == nullptr || device_get_type(i2s_controller) != &I2S_CONTROLLER_TYPE) {
        LOG_E(TAG, "I2S controller device is not valid");
        return ERROR_RESOURCE;
    }

    auto* data = new PdmMicData();
    data->native_channels = config->channels;
    data->input_gain = (float) config->input_gain_percent / 100.0f;

    data->data_if = audio_codec_adapter_new_i2s_pdm_data(i2s_controller);
    if (data->data_if == nullptr) {
        LOG_E(TAG, "Failed to create I2S PDM data adapter");
        goto cleanup;
    }

    if (data->data_if->open(data->data_if, nullptr, 0) != ESP_CODEC_DEV_OK) {
        LOG_E(TAG, "Failed to open data interface");
        goto cleanup;
    }

    {
        dummy_codec_cfg_t codec_config = {
            .gpio_if = nullptr,
            .pa_pin = -1,
            .pa_reverted = false,
        };

        data->codec_if = dummy_codec_new(&codec_config);
        if (data->codec_if == nullptr) {
            LOG_E(TAG, "Failed to create dummy codec interface");
            goto cleanup;
        }
    }

    {
        esp_codec_dev_cfg_t dev_config = {
            .dev_type = ESP_CODEC_DEV_TYPE_IN,
            .codec_if = data->codec_if,
            .data_if = data->data_if,
        };

        data->codec_device = esp_codec_dev_new(&dev_config);
        if (data->codec_device == nullptr) {
            LOG_E(TAG, "Failed to create codec device");
            goto cleanup;
        }
    }

    device_set_driver_data(device, data);
    return ERROR_NONE;

cleanup:
    // Mirrors stop_device's teardown order -- delete_*_if() routines close their interface
    // first, so we don't need separate ->close() calls here.
    if (data->codec_device != nullptr) {
        esp_codec_dev_delete(data->codec_device);
    }
    if (data->codec_if != nullptr) {
        audio_codec_delete_codec_if(data->codec_if);
    }
    if (data->data_if != nullptr) {
        audio_codec_delete_data_if(data->data_if);
    }
    delete data;
    return ERROR_RESOURCE;
}

error_t stop_device(Device* device) {
    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_NONE;
    }

    if (data->is_open) {
        esp_codec_dev_close(data->codec_device);
    }

    if (data->codec_device != nullptr) {
        esp_codec_dev_delete(data->codec_device);
    }
    if (data->codec_if != nullptr) {
        audio_codec_delete_codec_if(data->codec_if);
    }
    if (data->data_if != nullptr) {
        audio_codec_delete_data_if(data->data_if);
    }

    device_set_driver_data(device, nullptr);
    delete data;
    return ERROR_NONE;
}

// endregion

} // namespace

extern "C" {

Driver pdm_mic_driver = {
    .name = "pdm_mic",
    .compatible = (const char*[]) { "generic,spm1423", nullptr },
    .start_device = start_device,
    .stop_device = stop_device,
    .api = &API,
    .device_type = &AUDIO_CODEC_TYPE,
    .owner = nullptr,
    .internal = nullptr,
};

}
