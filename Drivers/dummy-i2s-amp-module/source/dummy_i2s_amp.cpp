// SPDX-License-Identifier: Apache-2.0
#include <drivers/dummy_i2s_amp.h>

#include <tactility/device.h>
#include <tactility/drivers/audio_codec.h>
#include <tactility/drivers/audio_codec_adapters.h>
#include <tactility/drivers/i2s_controller.h>
#include <tactility/log.h>

#include <dummy_codec.h>
#include <esp_codec_dev.h>
#include <audio_codec_sw_vol.h>

#include <cmath>

#define TAG "DummyI2sAmp"

namespace {

// A dumb I2S class-D amp has no native rate of its own -- it just clocks whatever I2S
// frames arrive. Report a common default; audio_stream resamples to whatever rate it is
// opened with.
constexpr uint32_t NATIVE_SAMPLE_RATE = 44100;

struct DummyI2sAmpData {
    const audio_codec_data_if_t* data_if = nullptr;
    const audio_codec_gpio_if_t* gpio_if = nullptr;
    const audio_codec_if_t* codec_if = nullptr;
    const audio_codec_vol_if_t* vol_if = nullptr;
    esp_codec_dev_handle_t codec_device = nullptr;
    bool is_open = false;
    esp_codec_dev_sample_info_t open_sample_info = {};

    // esp_codec_dev_set_out_mute() short-circuits on dummy_codec's mute callback (which
    // only toggles the PA enable GPIO, a no-op when there's no enable pin) and never
    // reaches its own software-volume mute fallback as a result. Track mute state
    // ourselves and implement it by zeroing/restoring volume through the working
    // esp_codec_dev_set_out_vol() path (confirmed working, unlike mute) instead.
    bool muted = false;
    int volume_percent = 100;
};

#define GET_CONFIG(device) (static_cast<const DummyI2sAmpConfig*>((device)->config))
#define GET_DATA(device) (static_cast<DummyI2sAmpData*>(device_get_driver_data(device)))

// region AudioCodecApi

error_t open(Device* device, const struct AudioCodecStreamConfig* config) {
    auto* data = GET_DATA(device);
    if (data->codec_device == nullptr) {
        return ERROR_RESOURCE;
    }

    if (config->direction != AUDIO_CODEC_DIR_OUTPUT && config->direction != AUDIO_CODEC_DIR_BOTH) {
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
    (void) device;
    (void) buffer;
    (void) size;
    (void) bytes_read;
    (void) timeout;
    return ERROR_NOT_SUPPORTED;
}

error_t write(Device* device, const void* buffer, size_t size, size_t* bytes_written, TickType_t timeout) {
    (void) timeout;
    auto* data = GET_DATA(device);
    if (!data->is_open) {
        return ERROR_RESOURCE;
    }

    // esp_codec_dev_write returns the number of bytes written (>= 0) on success, or a negative
    // ESP_CODEC_DEV_* error code on failure -- it does NOT return ESP_CODEC_DEV_OK (0) for
    // a successful nonzero-length write.
    int result = esp_codec_dev_write(data->codec_device, const_cast<void*>(buffer), (int) size);
    if (result < 0) {
        return ERROR_RESOURCE;
    }

    *bytes_written = (size_t) result;
    return ERROR_NONE;
}

error_t set_volume(Device* device, AudioCodecDirection direction, float volume_percent) {
    auto* data = GET_DATA(device);
    if (data->codec_device == nullptr) {
        return ERROR_RESOURCE;
    }

    if (direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }

    if (volume_percent < 0.0f || volume_percent > 100.0f) {
        return ERROR_RESOURCE;
    }

    data->volume_percent = (int) std::lround(volume_percent);

    // Don't push the new volume to hardware while muted -- that would audibly unmute.
    // It takes effect once set_mute(false) restores it.
    if (data->muted) {
        return ERROR_NONE;
    }

    return (esp_codec_dev_set_out_vol(data->codec_device, data->volume_percent) == ESP_CODEC_DEV_OK) ? ERROR_NONE : ERROR_RESOURCE;
}

error_t get_volume(Device* device, AudioCodecDirection direction, float* volume_percent) {
    auto* data = GET_DATA(device);
    if (data->codec_device == nullptr) {
        return ERROR_RESOURCE;
    }

    if (direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }

    *volume_percent = (float) data->volume_percent;
    return ERROR_NONE;
}

error_t set_mute(Device* device, AudioCodecDirection direction, bool muted) {
    auto* data = GET_DATA(device);
    if (data->codec_device == nullptr) {
        return ERROR_RESOURCE;
    }

    if (direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }

    if (data->muted == muted) {
        return ERROR_NONE;
    }

    // esp_codec_dev_set_out_mute() is unusable here -- see DummyI2sAmpData::muted comment.
    // Implement mute as a volume override through the working esp_codec_dev_set_out_vol()
    // path instead: zero the hardware volume while muted, restore the tracked percentage
    // on unmute.
    int target = muted ? 0 : data->volume_percent;
    if (esp_codec_dev_set_out_vol(data->codec_device, target) != ESP_CODEC_DEV_OK) {
        return ERROR_RESOURCE;
    }

    data->muted = muted;
    return ERROR_NONE;
}

error_t get_mute(Device* device, AudioCodecDirection direction, bool* muted) {
    auto* data = GET_DATA(device);

    if (direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }

    *muted = data->muted;
    return ERROR_NONE;
}

error_t get_native_channels(Device* device, AudioCodecDirection direction, uint8_t* channels) {
    (void) device;
    if (direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }
    *channels = 2;
    return ERROR_NONE;
}

error_t get_native_sample_rate(Device* device, AudioCodecDirection direction, uint32_t* rate_hz) {
    (void) device;
    if (direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }
    *rate_hz = NATIVE_SAMPLE_RATE;
    return ERROR_NONE;
}

error_t get_capabilities(Device* device, AudioCodecDirection* supported_directions) {
    (void) device;
    *supported_directions = AUDIO_CODEC_DIR_OUTPUT;
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
};

// endregion

// region Driver lifecycle

error_t start_device(Device* device) {
    const auto* config = GET_CONFIG(device);

    auto* i2s_controller = config->i2s_device;
    if (i2s_controller == nullptr || device_get_type(i2s_controller) != &I2S_CONTROLLER_TYPE) {
        LOG_E(TAG, "I2S controller device is not valid");
        return ERROR_RESOURCE;
    }

    auto* data = new DummyI2sAmpData();

    data->data_if = audio_codec_adapter_new_i2s_data(i2s_controller);
    if (data->data_if == nullptr) {
        LOG_E(TAG, "Failed to create I2S data adapter");
        goto cleanup;
    }

    if (data->data_if->open(data->data_if, nullptr, 0) != ESP_CODEC_DEV_OK) {
        LOG_E(TAG, "Failed to open data interface");
        goto cleanup;
    }

    {
        int16_t pa_pin = -1;
        if (config->enable_pin.gpio_controller != nullptr) {
            data->gpio_if = audio_codec_adapter_new_gpio(&config->enable_pin, 1);
            if (data->gpio_if == nullptr) {
                LOG_E(TAG, "Failed to create GPIO adapter for enable pin");
                goto cleanup;
            }
            pa_pin = 0;
        }

        dummy_codec_cfg_t codec_config = {
            .gpio_if = data->gpio_if,
            .pa_pin = pa_pin,
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
            .dev_type = ESP_CODEC_DEV_TYPE_OUT,
            .codec_if = data->codec_if,
            .data_if = data->data_if,
        };

        data->codec_device = esp_codec_dev_new(&dev_config);
        if (data->codec_device == nullptr) {
            LOG_E(TAG, "Failed to create codec device");
            goto cleanup;
        }
    }

    // Dumb I2S amps have no hardware volume/mute registers -- apply volume/mute in
    // software on the PCM stream instead.
    data->vol_if = audio_codec_new_sw_vol();
    if (data->vol_if == nullptr || esp_codec_dev_set_vol_handler(data->codec_device, data->vol_if) != ESP_CODEC_DEV_OK) {
        LOG_E(TAG, "Failed to install software volume handler");
        goto cleanup;
    }

    device_set_driver_data(device, data);
    return ERROR_NONE;

cleanup:
    // Mirrors stop_device's teardown order -- delete_*_if() routines close their interface
    // first, so we don't need separate ->close() calls here.
    if (data->codec_device != nullptr) {
        esp_codec_dev_delete(data->codec_device);
    }
    if (data->vol_if != nullptr) {
        audio_codec_delete_vol_if(data->vol_if);
    }
    if (data->codec_if != nullptr) {
        audio_codec_delete_codec_if(data->codec_if);
    }
    if (data->gpio_if != nullptr) {
        audio_codec_adapter_delete_gpio(data->gpio_if);
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
    if (data->vol_if != nullptr) {
        audio_codec_delete_vol_if(data->vol_if);
    }
    if (data->codec_if != nullptr) {
        audio_codec_delete_codec_if(data->codec_if);
    }
    if (data->gpio_if != nullptr) {
        audio_codec_adapter_delete_gpio(data->gpio_if);
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

Driver dummy_i2s_amp_driver = {
    .name = "dummy_i2s_amp",
    .compatible = (const char*[]) { "maxim,max98357a", "nsiway,ns4168", nullptr },
    .start_device = start_device,
    .stop_device = stop_device,
    .api = &API,
    .device_type = &AUDIO_CODEC_TYPE,
    .owner = nullptr,
    .internal = nullptr,
};

}
