// SPDX-License-Identifier: Apache-2.0
#include <drivers/es7210.h>

#include <tactility/device.h>
#include <tactility/drivers/audio_codec.h>
#include <tactility/drivers/audio_codec_adapters.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/i2s_controller.h>
#include <tactility/log.h>

#include <es7210_adc.h>
#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>

#include <cstring>
#include <vector>

#define TAG "ES7210"

namespace {

constexpr uint32_t NATIVE_SAMPLE_RATE = 16000;
constexpr float MAX_INPUT_GAIN_DB = 37.5f; // ES7210 mic gain range is roughly 0..37.5 dB

struct Es7210Data {
    const audio_codec_ctrl_if_t* ctrl_if = nullptr;
    const audio_codec_data_if_t* data_if = nullptr;
    const audio_codec_if_t* codec_if = nullptr;
    esp_codec_dev_handle_t codec_device = nullptr;
    bool is_open = false;
    uint8_t mic_count = 4;
    uint8_t mic_mask = 0x0F;
    uint8_t tdm_slot_count = 4;
    uint8_t open_bits_per_sample = 16;
    uint32_t open_sample_rate = 0;
    float input_gain = 1.0f;
    std::vector<uint8_t> raw_buffer;
};

#define GET_CONFIG(device) (static_cast<const Es7210Config*>((device)->config))
#define GET_DATA(device) (static_cast<Es7210Data*>(device_get_driver_data(device)))

// region AudioCodecApi

error_t open(Device* device, const struct AudioCodecStreamConfig* config) {
    auto* data = GET_DATA(device);
    if (data->codec_device == nullptr) {
        return ERROR_RESOURCE;
    }

    if (config->direction == AUDIO_CODEC_DIR_OUTPUT || config->direction == AUDIO_CODEC_DIR_BOTH) {
        LOG_E(TAG, "ES7210 is input-only");
        return ERROR_NOT_SUPPORTED;
    }

    if (data->is_open) {
        bool same_config = config->bits_per_sample == data->open_bits_per_sample
            && config->sample_rate == data->open_sample_rate;
        return same_config ? ERROR_NONE : ERROR_RESOURCE;
    }

    // Open with tdm_slot_count channels, not the (possibly smaller) active mic count.
    // tdm_slot_count is set in start_device() to match whichever framing the vendor
    // driver will actually use: the full 4-slot TDM frame when TDM mode engages
    // (mic_count >= 3), or exactly mic_count when it doesn't (plain N-channel I2S --
    // see start_device()'s tdm_slot_count comment). Opening with the wrong channel
    // count here mismatches the I2S receive layout the codec actually produces,
    // corrupting every channel past the first. When TDM *is* engaged, using the full
    // slot count (rather than just the active mic count) also sidesteps a separate
    // vendor quirk: es7210_set_fs() silently halves the configured bit depth when asked
    // for <= 2 channels in TDM mode (channel_mask == 0). We demux down to the active
    // mic slots ourselves in read().
    uint8_t hw_channels = data->tdm_slot_count;

    esp_codec_dev_sample_info_t sample_info = {
        .bits_per_sample = config->bits_per_sample,
        .channel = hw_channels,
        .channel_mask = 0,
        .sample_rate = config->sample_rate,
        .mclk_multiple = 0,
    };

    if (esp_codec_dev_open(data->codec_device, &sample_info) != ESP_CODEC_DEV_OK) {
        LOG_E(TAG, "Failed to open codec device");
        return ERROR_RESOURCE;
    }

    data->open_bits_per_sample = config->bits_per_sample;
    data->open_sample_rate = config->sample_rate;
    data->is_open = true;
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

    // The hardware always runs at tdm_slot_count channels (see open() for why); demux down
    // to just the active mic slots here so callers see exactly mic_count channels of real
    // signal -- never diluted by silent/AEC-reference TDM slots.
    if (data->mic_count == data->tdm_slot_count) {
        int result = esp_codec_dev_read(data->codec_device, buffer, (int) size);
        if (result < 0) {
            return ERROR_RESOURCE;
        }
        *bytes_read = (size_t) result;
        return ERROR_NONE;
    }

    uint8_t bytes_per_sample = (uint8_t) (data->open_bits_per_sample / 8);
    size_t out_frame_bytes = (size_t) data->mic_count * bytes_per_sample;
    size_t requested_frames = (out_frame_bytes != 0) ? (size / out_frame_bytes) : 0;
    if (requested_frames == 0) {
        *bytes_read = 0;
        return ERROR_NONE;
    }

    size_t raw_frame_bytes = (size_t) data->tdm_slot_count * bytes_per_sample;
    size_t raw_bytes_needed = requested_frames * raw_frame_bytes;
    if (data->raw_buffer.size() < raw_bytes_needed) {
        data->raw_buffer.resize(raw_bytes_needed);
    }

    int result = esp_codec_dev_read(data->codec_device, data->raw_buffer.data(), (int) raw_bytes_needed);
    if (result < 0) {
        return ERROR_RESOURCE;
    }

    size_t raw_bytes_read = (size_t) result;
    size_t frames_read = raw_bytes_read / raw_frame_bytes;

    // Demux: pick the bytes_per_sample-wide slot for each set bit in mic_mask, in ascending
    // slot order, e.g. mic_mask = MIC1|MIC3 -> slots 0 and 2.
    uint8_t slot_indices[8];
    uint8_t slot_count = 0;
    for (uint8_t slot = 0; slot < data->tdm_slot_count && slot_count < sizeof(slot_indices); slot++) {
        if ((data->mic_mask & (uint8_t) (1u << slot)) != 0) {
            slot_indices[slot_count++] = slot;
        }
    }

    auto* out = static_cast<uint8_t*>(buffer);
    const uint8_t* raw = data->raw_buffer.data();
    for (size_t frame = 0; frame < frames_read; frame++) {
        const uint8_t* raw_frame = raw + frame * raw_frame_bytes;
        uint8_t* out_frame = out + frame * out_frame_bytes;
        for (uint8_t ch = 0; ch < slot_count && ch < data->mic_count; ch++) {
            std::memcpy(out_frame + (size_t) ch * bytes_per_sample, raw_frame + (size_t) slot_indices[ch] * bytes_per_sample, bytes_per_sample);
        }
    }

    *bytes_read = frames_read * out_frame_bytes;
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

error_t set_volume(Device* device, AudioCodecDirection direction, float volume_percent) {
    auto* data = GET_DATA(device);
    if (data->codec_device == nullptr || direction != AUDIO_CODEC_DIR_INPUT) {
        return ERROR_NOT_SUPPORTED;
    }

    // Map 0..100% linearly onto the gain range.
    float db = (volume_percent / 100.0f) * MAX_INPUT_GAIN_DB;
    return (esp_codec_dev_set_in_gain(data->codec_device, db) == ESP_CODEC_DEV_OK) ? ERROR_NONE : ERROR_RESOURCE;
}

error_t get_volume(Device* device, AudioCodecDirection direction, float* volume_percent) {
    auto* data = GET_DATA(device);
    if (data->codec_device == nullptr || direction != AUDIO_CODEC_DIR_INPUT) {
        return ERROR_NOT_SUPPORTED;
    }

    float db = 0.0f;
    if (esp_codec_dev_get_in_gain(data->codec_device, &db) != ESP_CODEC_DEV_OK) {
        return ERROR_RESOURCE;
    }
    *volume_percent = (db / MAX_INPUT_GAIN_DB) * 100.0f;
    return ERROR_NONE;
}

error_t set_mute(Device* device, AudioCodecDirection direction, bool muted) {
    auto* data = GET_DATA(device);
    if (data->codec_device == nullptr || direction != AUDIO_CODEC_DIR_INPUT) {
        return ERROR_NOT_SUPPORTED;
    }

    return (esp_codec_dev_set_in_mute(data->codec_device, muted) == ESP_CODEC_DEV_OK) ? ERROR_NONE : ERROR_RESOURCE;
}

error_t get_mute(Device* device, AudioCodecDirection direction, bool* muted) {
    auto* data = GET_DATA(device);
    if (data->codec_device == nullptr || direction != AUDIO_CODEC_DIR_INPUT) {
        return ERROR_NOT_SUPPORTED;
    }

    return (esp_codec_dev_get_in_mute(data->codec_device, muted) == ESP_CODEC_DEV_OK) ? ERROR_NONE : ERROR_RESOURCE;
}

error_t get_native_channels(Device* device, AudioCodecDirection direction, uint8_t* channels) {
    auto* data = GET_DATA(device);
    if (direction != AUDIO_CODEC_DIR_INPUT) {
        return ERROR_NOT_SUPPORTED;
    }
    *channels = data->mic_count;
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

    // devicetree's int property type has no min/max constraint support, so a bogus
    // negative literal would otherwise silently wrap to 65535 when narrowed to uint16_t.
    // Bound it here instead -- 2000 (20x) is already an extreme upper bound for
    // compensating a quiet mic capsule on top of the ES7210's own hardware gain.
    if (config->input_gain_percent > 2000) {
        LOG_E(TAG, "Invalid input_gain_percent %u (must be 0..2000)", config->input_gain_percent);
        return ERROR_RESOURCE;
    }

    // Same rationale as input_gain_percent: devicetree ints aren't bit-constrained, so
    // reject any bit outside the four supported mic slots before it inflates mic_count
    // beyond the fixed 4-slot TDM frame that read() demuxes against.
    constexpr uint8_t supported_mic_mask = (uint8_t) (ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4);
    if ((config->mic_selected_mask & ~supported_mic_mask) != 0) {
        LOG_E(TAG, "Invalid mic_selected_mask 0x%02X (must be subset of 0x%02X)", config->mic_selected_mask, supported_mic_mask);
        return ERROR_RESOURCE;
    }

    auto* i2c_controller = device_get_parent(device);
    if (i2c_controller == nullptr || device_get_type(i2c_controller) != &I2C_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent is not an I2C controller");
        return ERROR_RESOURCE;
    }

    auto* i2s_controller = config->i2s_device;
    if (i2s_controller == nullptr || device_get_type(i2s_controller) != &I2S_CONTROLLER_TYPE) {
        LOG_E(TAG, "I2S controller device is not valid");
        return ERROR_RESOURCE;
    }

    auto* data = new Es7210Data();

    uint8_t mic_mask = (config->mic_selected_mask != 0)
        ? config->mic_selected_mask
        : (uint8_t) (ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4);
    data->mic_mask = mic_mask;
    data->mic_count = (uint8_t) __builtin_popcount(mic_mask);
    // Mirrors the vendor driver's own TDM threshold (es7210_is_tdm_mode(): TDM only
    // engages when mic_num >= 3) -- below that it runs a plain N-channel I2S frame, not
    // a fixed 4-slot TDM frame. Forcing a 4-channel open (see open()'s hw_channels
    // comment) against a non-TDM 2-mic config mismatches the I2S receive layout the
    // codec is actually producing, corrupting every channel past the first.
    data->tdm_slot_count = (data->mic_count >= 3) ? 4 : data->mic_count;
    data->input_gain = (float) config->input_gain_percent / 100.0f;

    data->ctrl_if = audio_codec_adapter_new_i2c_ctrl(i2c_controller, config->address);
    data->data_if = audio_codec_adapter_new_i2s_data(i2s_controller);
    if (data->ctrl_if == nullptr || data->data_if == nullptr) {
        LOG_E(TAG, "Failed to create adapters");
        goto cleanup;
    }

    if (data->ctrl_if->open(data->ctrl_if, nullptr, 0) != ESP_CODEC_DEV_OK) {
        LOG_E(TAG, "Failed to open control interface");
        goto cleanup;
    }

    if (data->data_if->open(data->data_if, nullptr, 0) != ESP_CODEC_DEV_OK) {
        LOG_E(TAG, "Failed to open data interface");
        goto cleanup;
    }

    {
        es7210_codec_cfg_t codec_config = {};
        codec_config.ctrl_if = data->ctrl_if;
        codec_config.master_mode = false;
        codec_config.mic_selected = mic_mask;
        codec_config.mclk_src = ES7210_MCLK_FROM_PAD;
        codec_config.mclk_div = 0;

        data->codec_if = es7210_codec_new(&codec_config);
        if (data->codec_if == nullptr) {
            LOG_E(TAG, "Failed to create ES7210 codec interface");
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
    if (data->ctrl_if != nullptr) {
        audio_codec_delete_ctrl_if(data->ctrl_if);
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
    if (data->ctrl_if != nullptr) {
        audio_codec_delete_ctrl_if(data->ctrl_if);
    }

    device_set_driver_data(device, nullptr);
    delete data;
    return ERROR_NONE;
}

// endregion

} // namespace

extern "C" {

Driver es7210_driver = {
    .name = "es7210",
    .compatible = (const char*[]) { "everest,es7210", nullptr },
    .start_device = start_device,
    .stop_device = stop_device,
    .api = &API,
    .device_type = &AUDIO_CODEC_TYPE,
    .owner = nullptr,
    .internal = nullptr,
};

}
