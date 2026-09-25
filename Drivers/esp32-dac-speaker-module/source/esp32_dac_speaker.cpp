// SPDX-License-Identifier: Apache-2.0
#include <drivers/esp32_dac_speaker.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/audio_codec.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <cstddef>
#include <cstdint>
#include <new>

#include "freertos/FreeRTOS.h"
#include "soc/soc.h"

#if SOC_DAC_SUPPORTED
#include "driver/dac_continuous.h"
#include "soc/dac_channel.h"
#endif

#define TAG "Esp32DacSpeaker"

namespace {

// The ESP32 DAC is 8-bit.
constexpr size_t CONVERT_CHUNK_SAMPLES = 1024;
constexpr uint32_t DMA_DESC_COUNT = 8;
constexpr size_t DMA_BUF_SIZE = 2048; // 1024 samples per descriptor, 8 descriptors = 512 ms at 16 kHz

// The DAC digital controller on ESP32 can only reach audio rates through the APLL clock source
constexpr uint8_t MID_DAC_LEVEL = 128;

#if SOC_DAC_SUPPORTED
struct Esp32DacSpeakerInternal {
    dac_continuous_handle_t handle = nullptr;
    struct GpioDescriptor* pin_descriptor = nullptr;

    // Open stream state.
    bool is_open = false;
    uint8_t open_bits_per_sample = 0;
    uint32_t open_sample_rate = 0;

    // Software volume/mute
    int volume_percent = 100;
    bool muted = false;

    uint8_t scratch[CONVERT_CHUNK_SAMPLES];
};
#endif

#define GET_CONFIG(device) (static_cast<const Esp32DacSpeakerConfig*>((device)->config))
#define GET_DATA(device) (static_cast<Esp32DacSpeakerInternal*>(device_get_driver_data(device)))

#if SOC_DAC_SUPPORTED

dac_channel_mask_t pin_to_channel_mask(uint8_t pin) {
    if (pin == DAC_CHAN0_GPIO_NUM) {
        return DAC_CHANNEL_MASK_CH0;
    }
    if (pin == DAC_CHAN1_GPIO_NUM) {
        return DAC_CHANNEL_MASK_CH1;
    }
    return 0;
}

int ticks_to_timeout_ms(TickType_t ticks) {
    if (ticks == portMAX_DELAY) {
        return -1;
    }
    return (int) (((uint64_t) ticks) * 1000 / configTICK_RATE_HZ);
}

// Create (or recreate, on a rate change) the DAC continuous channel at the given rate
error_t create_channel(Esp32DacSpeakerInternal* data, const Esp32DacSpeakerConfig* config, uint32_t sample_rate) {
    dac_channel_mask_t mask = pin_to_channel_mask(config->pin.pin);
    if (mask == 0) {
        LOG_E(TAG, "Pin %u is not a DAC channel (ESP32: GPIO25 or GPIO26)", config->pin.pin);
        return ERROR_NOT_SUPPORTED;
    }

    if (data->handle != nullptr) {
        dac_continuous_disable(data->handle);
        dac_continuous_del_channels(data->handle);
        data->handle = nullptr;
    }

    const dac_continuous_config_t cfg = {
        .chan_mask = mask,
        .desc_num = DMA_DESC_COUNT,
        .buf_size = DMA_BUF_SIZE,
        .freq_hz = sample_rate,
        .offset = 0,
        .clk_src = DAC_DIGI_CLK_SRC_APLL,
        .chan_mode = DAC_CHANNEL_MODE_SIMUL,
    };

    if (dac_continuous_new_channels(&cfg, &data->handle) != ESP_OK) {
        LOG_E(TAG, "Failed to create continuous DAC channel at %lu Hz", (unsigned long) sample_rate);
        data->handle = nullptr;
        return ERROR_RESOURCE;
    }

    if (dac_continuous_enable(data->handle) != ESP_OK) {
        LOG_E(TAG, "Failed to enable continuous DAC channel");
        dac_continuous_del_channels(data->handle);
        data->handle = nullptr;
        return ERROR_RESOURCE;
    }

    return ERROR_NONE;
}

// Convert a chunk of 16-bit mono PCM into 8-bit DAC samples with software volume/mute applied.
void convert_pcm16_to_dac8(
    Esp32DacSpeakerInternal* data,
    const int16_t* pcm,
    size_t samples,
    int volume_percent,
    uint8_t* out
) {
    for (size_t i = 0; i < samples; i++) {
        int32_t scaled = ((int32_t) pcm[i] * volume_percent) / 100;
        out[i] = data->muted ? MID_DAC_LEVEL : (uint8_t) ((scaled >> 8) + 128);
    }
}

// Same, for already-8-bit mono PCM (values 0..255, centered on 128).
void convert_pcm8_to_dac8(
    Esp32DacSpeakerInternal* data,
    const uint8_t* pcm,
    size_t samples,
    int volume_percent,
    uint8_t* out
) {
    for (size_t i = 0; i < samples; i++) {
        int32_t scaled = (((int32_t) pcm[i] - 128) * volume_percent) / 100;
        int32_t level = scaled + 128;
        out[i] = data->muted ? MID_DAC_LEVEL : (uint8_t) (level < 0 ? 0 : (level > 255 ? 255 : level));
    }
}

#endif // SOC_DAC_SUPPORTED

// region AudioCodecApi

error_t open(Device* device, const struct AudioCodecStreamConfig* config) {
#if SOC_DAC_SUPPORTED
    auto* data = GET_DATA(device);
    const auto* cfg = GET_CONFIG(device);
    if (data == nullptr || cfg == nullptr) {
        return ERROR_RESOURCE;
    }

    if (config->direction != AUDIO_CODEC_DIR_OUTPUT && config->direction != AUDIO_CODEC_DIR_BOTH) {
        return ERROR_NOT_SUPPORTED;
    }
    if (config->bits_per_sample != 8 && config->bits_per_sample != 16) {
        return ERROR_NOT_SUPPORTED;
    }
    if (config->channels != 1) {
        return ERROR_NOT_SUPPORTED;
    }
    if (config->sample_rate == 0) {
        return ERROR_INVALID_ARGUMENT;
    }

    if (data->is_open) {
        bool same_config = data->open_bits_per_sample == config->bits_per_sample
            && data->open_sample_rate == config->sample_rate;
        return same_config ? ERROR_NONE : ERROR_RESOURCE;
    }

    if (create_channel(data, cfg, config->sample_rate) != ERROR_NONE) {
        return ERROR_RESOURCE;
    }

    data->open_bits_per_sample = config->bits_per_sample;
    data->open_sample_rate = config->sample_rate;
    data->is_open = true;
    return ERROR_NONE;
#else
    return ERROR_NOT_SUPPORTED;
#endif
}

error_t close(Device* device) {
#if SOC_DAC_SUPPORTED
    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_RESOURCE;
    }

    if (data->is_open && data->handle != nullptr) {
        dac_continuous_disable(data->handle);
        dac_continuous_del_channels(data->handle);
        data->handle = nullptr;
    }

    data->is_open = false;
    data->open_bits_per_sample = 0;
    data->open_sample_rate = 0;
    return ERROR_NONE;
#else
    return ERROR_NOT_SUPPORTED;
#endif
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
#if SOC_DAC_SUPPORTED
    auto* data = GET_DATA(device);
    if (data == nullptr || !data->is_open || data->handle == nullptr) {
        return ERROR_RESOURCE;
    }

    const size_t frame_size = data->open_bits_per_sample / 8;
    const size_t whole_bytes = size - (size % frame_size); // drop partial trailing frames
    const size_t frame_count = whole_bytes / frame_size;

    const uint8_t* input = static_cast<const uint8_t*>(buffer);
    const int timeout_ms = ticks_to_timeout_ms(timeout);

    size_t frames_done = 0;
    while (frames_done < frame_count) {
        const size_t chunk = frame_count - frames_done < CONVERT_CHUNK_SAMPLES
            ? frame_count - frames_done
            : CONVERT_CHUNK_SAMPLES;

        if (data->open_bits_per_sample == 16) {
            convert_pcm16_to_dac8(data, reinterpret_cast<const int16_t*>(input + frames_done * frame_size), chunk, data->volume_percent, data->scratch);
        } else {
            convert_pcm8_to_dac8(data, input + frames_done * frame_size, chunk, data->volume_percent, data->scratch);
        }

        size_t loaded = 0;
        esp_err_t result = dac_continuous_write(data->handle, data->scratch, chunk, &loaded, timeout_ms);
        if (result != ESP_OK) {
            *bytes_written = (frames_done + loaded) * frame_size;
            return result == ESP_ERR_TIMEOUT ? ERROR_TIMEOUT : ERROR_RESOURCE;
        }

        frames_done += chunk;
    }

    *bytes_written = frame_count * frame_size;
    return ERROR_NONE;
#else
    return ERROR_NOT_SUPPORTED;
#endif
}

error_t set_volume(Device* device, AudioCodecDirection direction, float volume_percent) {
#if SOC_DAC_SUPPORTED
    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_RESOURCE;
    }
    if (direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }
    if (volume_percent < 0.0f || volume_percent > 100.0f) {
        return ERROR_INVALID_ARGUMENT;
    }

    data->volume_percent = (int) (volume_percent + 0.5f);
    return ERROR_NONE;
#else
    return ERROR_NOT_SUPPORTED;
#endif
}

error_t get_volume(Device* device, AudioCodecDirection direction, float* volume_percent) {
#if SOC_DAC_SUPPORTED
    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_RESOURCE;
    }
    if (direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }

    *volume_percent = (float) data->volume_percent;
    return ERROR_NONE;
#else
    return ERROR_NOT_SUPPORTED;
#endif
}

error_t set_mute(Device* device, AudioCodecDirection direction, bool muted) {
#if SOC_DAC_SUPPORTED
    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_RESOURCE;
    }
    if (direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }

    data->muted = muted;
    return ERROR_NONE;
#else
    return ERROR_NOT_SUPPORTED;
#endif
}

error_t get_mute(Device* device, AudioCodecDirection direction, bool* muted) {
#if SOC_DAC_SUPPORTED
    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_RESOURCE;
    }
    if (direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }

    *muted = data->muted;
    return ERROR_NONE;
#else
    return ERROR_NOT_SUPPORTED;
#endif
}

error_t get_native_channels(Device* device, AudioCodecDirection direction, uint8_t* channels) {
    (void) device;
    if (direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }
    *channels = 1;
    return ERROR_NONE;
}

error_t get_native_sample_rate(Device* device, AudioCodecDirection direction, uint32_t* rate_hz) {
    if (direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }
    auto* config = GET_CONFIG(device);
    if (config != nullptr) {
        *rate_hz = config->sample_rate;
        return ERROR_NONE;
    }
    return ERROR_RESOURCE;
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
#if SOC_DAC_SUPPORTED
    const auto* config = GET_CONFIG(device);
    if (config == nullptr) {
        return ERROR_RESOURCE;
    }

    if (pin_to_channel_mask(config->pin.pin) == 0) {
        LOG_E(TAG, "Pin %u is not an ESP32 DAC channel (GPIO25 = DAC1, GPIO26 = DAC2)", config->pin.pin);
        return ERROR_NOT_SUPPORTED;
    }

    auto* data = new (std::nothrow) Esp32DacSpeakerInternal();
    if (data == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    if (config->pin.gpio_controller != nullptr) {
        // GPIO_OWNER_PERIPHERAL: the DAC driver configures the pin's mux/function itself, so we
        // only reserve ownership to keep it out of other consumers' hands.
        data->pin_descriptor = gpio_descriptor_acquire(
            config->pin.gpio_controller,
            config->pin.pin,
            config->pin.flags | GPIO_FLAG_DIRECTION_OUTPUT,
            GPIO_OWNER_PERIPHERAL
        );
        if (data->pin_descriptor == nullptr) {
            LOG_E(TAG, "Failed to reserve DAC pin %u", config->pin.pin);
            delete data;
            return ERROR_RESOURCE;
        }
    }

    device_set_driver_data(device, data);
    return ERROR_NONE;
#else
    (void) device;
    return ERROR_NOT_SUPPORTED;
#endif
}

error_t stop_device(Device* device) {
#if SOC_DAC_SUPPORTED
    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_NONE;
    }

    if (data->handle != nullptr) {
        dac_continuous_disable(data->handle);
        dac_continuous_del_channels(data->handle);
        data->handle = nullptr;
    }

    if (data->pin_descriptor != nullptr) {
        gpio_descriptor_release(data->pin_descriptor);
        data->pin_descriptor = nullptr;
    }

    device_set_driver_data(device, nullptr);
    delete data;
    return ERROR_NONE;
#else
    (void) device;
    return ERROR_NONE;
#endif
}

// endregion

} // namespace

extern "C" {

Driver esp32_dac_speaker_driver = {
    .name = "esp32_dac_speaker",
    .compatible = (const char*[]) { "espressif,esp32-dac-speaker", nullptr },
    .start_device = start_device,
    .stop_device = stop_device,
    .api = &API,
    .device_type = &AUDIO_CODEC_TYPE,
    .owner = nullptr,
    .internal = nullptr,
};

}