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
    const audio_codec_data_if_t* dataIf = nullptr;
    const audio_codec_gpio_if_t* gpioIf = nullptr;
    const audio_codec_if_t* codecIf = nullptr;
    const audio_codec_vol_if_t* volIf = nullptr;
    esp_codec_dev_handle_t codecDevice = nullptr;
    bool isOpen = false;
    esp_codec_dev_sample_info_t openSampleInfo = {};

    // esp_codec_dev_set_out_mute() short-circuits on dummy_codec's mute callback (which
    // only toggles the PA enable GPIO, a no-op when there's no enable pin) and never
    // reaches its own software-volume mute fallback as a result. Track mute state
    // ourselves and implement it by zeroing/restoring volume through the working
    // esp_codec_dev_set_out_vol() path (confirmed working, unlike mute) instead.
    bool muted = false;
    int volumePercent = 100;
};

#define GET_CONFIG(device) (static_cast<const DummyI2sAmpConfig*>((device)->config))
#define GET_DATA(device) (static_cast<DummyI2sAmpData*>(device_get_driver_data(device)))

// region AudioCodecApi

error_t open_(Device* device, const struct AudioCodecStreamConfig* config) {
    auto* data = GET_DATA(device);
    if (data == nullptr || data->codecDevice == nullptr) {
        return ERROR_RESOURCE;
    }

    if (config->direction != AUDIO_CODEC_DIR_OUTPUT && config->direction != AUDIO_CODEC_DIR_BOTH) {
        return ERROR_NOT_SUPPORTED;
    }

    esp_codec_dev_sample_info_t sampleInfo = {
        .bits_per_sample = config->bits_per_sample,
        .channel = config->channels,
        .channel_mask = 0,
        .sample_rate = config->sample_rate,
        .mclk_multiple = 0,
    };

    if (data->isOpen) {
        bool sameConfig = data->openSampleInfo.bits_per_sample == sampleInfo.bits_per_sample
            && data->openSampleInfo.channel == sampleInfo.channel
            && data->openSampleInfo.sample_rate == sampleInfo.sample_rate;
        return sameConfig ? ERROR_NONE : ERROR_RESOURCE;
    }

    if (esp_codec_dev_open(data->codecDevice, &sampleInfo) != ESP_CODEC_DEV_OK) {
        LOG_E(TAG, "Failed to open codec device");
        return ERROR_RESOURCE;
    }

    data->isOpen = true;
    data->openSampleInfo = sampleInfo;
    return ERROR_NONE;
}

error_t close_(Device* device) {
    auto* data = GET_DATA(device);
    if (data == nullptr || data->codecDevice == nullptr) {
        return ERROR_RESOURCE;
    }

    if (data->isOpen) {
        esp_codec_dev_close(data->codecDevice);
        data->isOpen = false;
    }

    return ERROR_NONE;
}

error_t read_(Device* device, void* data_, size_t size, size_t* bytesRead, TickType_t timeout) {
    (void) device;
    (void) data_;
    (void) size;
    (void) bytesRead;
    (void) timeout;
    return ERROR_NOT_SUPPORTED;
}

error_t write_(Device* device, const void* buffer, size_t size, size_t* bytesWritten, TickType_t timeout) {
    (void) timeout;
    auto* data = GET_DATA(device);
    if (data == nullptr || !data->isOpen) {
        return ERROR_RESOURCE;
    }

    // esp_codec_dev_write returns the number of bytes written (>= 0) on success, or a negative
    // ESP_CODEC_DEV_* error code on failure -- it does NOT return ESP_CODEC_DEV_OK (0) for
    // a successful nonzero-length write.
    int result = esp_codec_dev_write(data->codecDevice, const_cast<void*>(buffer), (int) size);
    if (result < 0) {
        return ERROR_RESOURCE;
    }

    if (bytesWritten != nullptr) {
        *bytesWritten = (size_t) result;
    }
    return ERROR_NONE;
}

error_t setVolume(Device* device, enum AudioCodecDirection direction, float volumePercent) {
    auto* data = GET_DATA(device);
    if (data == nullptr || data->codecDevice == nullptr) {
        return ERROR_RESOURCE;
    }

    if (direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }

    if (volumePercent < 0.0f || volumePercent > 100.0f) {
        return ERROR_RESOURCE;
    }

    data->volumePercent = (int) std::lround(volumePercent);

    // Don't push the new volume to hardware while muted -- that would audibly unmute.
    // It takes effect once setMute(false) restores it.
    if (data->muted) {
        return ERROR_NONE;
    }

    return (esp_codec_dev_set_out_vol(data->codecDevice, data->volumePercent) == ESP_CODEC_DEV_OK) ? ERROR_NONE : ERROR_RESOURCE;
}

error_t getVolume(Device* device, enum AudioCodecDirection direction, float* volumePercent) {
    auto* data = GET_DATA(device);
    if (data == nullptr || data->codecDevice == nullptr || volumePercent == nullptr) {
        return ERROR_RESOURCE;
    }

    if (direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }

    *volumePercent = (float) data->volumePercent;
    return ERROR_NONE;
}

error_t setMute(Device* device, enum AudioCodecDirection direction, bool muted) {
    auto* data = GET_DATA(device);
    if (data == nullptr || data->codecDevice == nullptr) {
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
    int target = muted ? 0 : data->volumePercent;
    if (esp_codec_dev_set_out_vol(data->codecDevice, target) != ESP_CODEC_DEV_OK) {
        return ERROR_RESOURCE;
    }

    data->muted = muted;
    return ERROR_NONE;
}

error_t getMute(Device* device, enum AudioCodecDirection direction, bool* muted) {
    auto* data = GET_DATA(device);
    if (data == nullptr || muted == nullptr) {
        return ERROR_RESOURCE;
    }

    if (direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }

    *muted = data->muted;
    return ERROR_NONE;
}

error_t getNativeChannels(Device* device, enum AudioCodecDirection direction, uint8_t* channels) {
    (void) device;
    if (direction != AUDIO_CODEC_DIR_OUTPUT || channels == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    *channels = 2;
    return ERROR_NONE;
}

error_t getNativeSampleRate(Device* device, enum AudioCodecDirection direction, uint32_t* rateHz) {
    (void) device;
    if (direction != AUDIO_CODEC_DIR_OUTPUT || rateHz == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    *rateHz = NATIVE_SAMPLE_RATE;
    return ERROR_NONE;
}

error_t getCapabilities(Device* device, enum AudioCodecDirection* supportedDirections) {
    (void) device;
    if (supportedDirections == nullptr) {
        return ERROR_RESOURCE;
    }
    *supportedDirections = AUDIO_CODEC_DIR_OUTPUT;
    return ERROR_NONE;
}

const struct AudioCodecApi API = {
    .open = open_,
    .close = close_,
    .read = read_,
    .write = write_,
    .set_volume = setVolume,
    .get_volume = getVolume,
    .set_mute = setMute,
    .get_mute = getMute,
    .get_native_sample_rate = getNativeSampleRate,
    .get_native_channels = getNativeChannels,
    .get_capabilities = getCapabilities,
};

// endregion

// region Driver lifecycle

error_t startDevice(Device* device) {
    const auto* config = GET_CONFIG(device);

    auto* i2sController = device_find_by_name(config->i2s_device_name);
    if (i2sController == nullptr || device_get_type(i2sController) != &I2S_CONTROLLER_TYPE) {
        LOG_E(TAG, "I2S controller '%s' not found", config->i2s_device_name);
        return ERROR_RESOURCE;
    }

    auto* data = new DummyI2sAmpData();

    data->dataIf = audio_codec_adapter_new_i2s_data(i2sController);
    if (data->dataIf == nullptr) {
        LOG_E(TAG, "Failed to create I2S data adapter");
        goto cleanup;
    }

    if (data->dataIf->open(data->dataIf, nullptr, 0) != ESP_CODEC_DEV_OK) {
        LOG_E(TAG, "Failed to open data interface");
        goto cleanup;
    }

    {
        int16_t paPin = -1;
        if (config->enable_pin.gpio_controller != nullptr) {
            data->gpioIf = audio_codec_adapter_new_gpio(&config->enable_pin, 1);
            if (data->gpioIf == nullptr) {
                LOG_E(TAG, "Failed to create GPIO adapter for enable pin");
                goto cleanup;
            }
            paPin = 0;
        }

        dummy_codec_cfg_t codecConfig = {
            .gpio_if = data->gpioIf,
            .pa_pin = paPin,
            .pa_reverted = false,
        };

        data->codecIf = dummy_codec_new(&codecConfig);
        if (data->codecIf == nullptr) {
            LOG_E(TAG, "Failed to create dummy codec interface");
            goto cleanup;
        }
    }

    {
        esp_codec_dev_cfg_t devConfig = {
            .dev_type = ESP_CODEC_DEV_TYPE_OUT,
            .codec_if = data->codecIf,
            .data_if = data->dataIf,
        };

        data->codecDevice = esp_codec_dev_new(&devConfig);
        if (data->codecDevice == nullptr) {
            LOG_E(TAG, "Failed to create codec device");
            goto cleanup;
        }
    }

    // Dumb I2S amps have no hardware volume/mute registers -- apply volume/mute in
    // software on the PCM stream instead.
    data->volIf = audio_codec_new_sw_vol();
    if (data->volIf == nullptr || esp_codec_dev_set_vol_handler(data->codecDevice, data->volIf) != ESP_CODEC_DEV_OK) {
        LOG_E(TAG, "Failed to install software volume handler");
        goto cleanup;
    }

    device_set_driver_data(device, data);
    return ERROR_NONE;

cleanup:
    // Mirrors stopDevice's teardown order -- delete_*_if() routines close their interface
    // first, so we don't need separate ->close() calls here.
    if (data->codecDevice != nullptr) {
        esp_codec_dev_delete(data->codecDevice);
    }
    if (data->volIf != nullptr) {
        audio_codec_delete_vol_if(data->volIf);
    }
    if (data->codecIf != nullptr) {
        audio_codec_delete_codec_if(data->codecIf);
    }
    if (data->gpioIf != nullptr) {
        audio_codec_adapter_delete_gpio(data->gpioIf);
    }
    if (data->dataIf != nullptr) {
        audio_codec_delete_data_if(data->dataIf);
    }
    delete data;
    return ERROR_RESOURCE;
}

error_t stopDevice(Device* device) {
    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_NONE;
    }

    if (data->isOpen) {
        esp_codec_dev_close(data->codecDevice);
    }

    if (data->codecDevice != nullptr) {
        esp_codec_dev_delete(data->codecDevice);
    }
    if (data->volIf != nullptr) {
        audio_codec_delete_vol_if(data->volIf);
    }
    if (data->codecIf != nullptr) {
        audio_codec_delete_codec_if(data->codecIf);
    }
    if (data->gpioIf != nullptr) {
        audio_codec_adapter_delete_gpio(data->gpioIf);
    }
    if (data->dataIf != nullptr) {
        audio_codec_delete_data_if(data->dataIf);
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
    .start_device = startDevice,
    .stop_device = stopDevice,
    .api = &API,
    .device_type = &AUDIO_CODEC_TYPE,
    .owner = nullptr,
    .internal = nullptr,
};

}
