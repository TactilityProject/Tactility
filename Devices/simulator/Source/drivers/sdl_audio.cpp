// SPDX-License-Identifier: Apache-2.0
#include <tactility/delay.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/audio_codec.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdint>
#include <new>
#include <vector>

constexpr auto* TAG = "SdlAudio";

namespace {

constexpr uint32_t NATIVE_SAMPLE_RATE = 44100;
constexpr uint8_t NATIVE_CHANNELS = 2;
// How much audio may wait in SDL's queue. Writes block beyond it, like a hardware buffer, so apps are paced.
constexpr uint32_t MAX_QUEUED_MS = 200;
constexpr uint32_t POLL_MS = 5;
constexpr uint32_t MAX_DRAIN_MS = 2000;

struct SdlAudioInternal {
    SDL_AudioDeviceID device_id = 0;
    uint32_t sample_rate = 0;
    uint8_t channels = 0;
    float volume_percent = 100.0f;
    bool muted = false;
};

#define GET_DATA(device) (static_cast<SdlAudioInternal*>(device_get_driver_data(device)))

error_t open(Device* device, const AudioCodecStreamConfig* config) {
    auto* data = GET_DATA(device);
    if (config->direction != AUDIO_CODEC_DIR_OUTPUT && config->direction != AUDIO_CODEC_DIR_BOTH) {
        return ERROR_NOT_SUPPORTED;
    }
    if (config->bits_per_sample != 16 || config->channels == 0 || config->channels > 2) {
        return ERROR_NOT_SUPPORTED;
    }
    if (config->sample_rate == 0) {
        return ERROR_INVALID_ARGUMENT;
    }
    if (data->device_id != 0) {
        const bool same = data->sample_rate == config->sample_rate && data->channels == config->channels;
        return same ? ERROR_NONE : ERROR_RESOURCE;
    }

    SDL_AudioSpec wanted = {};
    wanted.freq = static_cast<int>(config->sample_rate);
    wanted.format = AUDIO_S16SYS;
    wanted.channels = config->channels;
    wanted.samples = 1024;
    // No callback: the audio is queued with SDL_QueueAudio()
    data->device_id = SDL_OpenAudioDevice(nullptr, 0, &wanted, nullptr, 0);
    if (data->device_id == 0) {
        LOG_E(TAG, "SDL_OpenAudioDevice failed: %s", SDL_GetError());
        return ERROR_RESOURCE;
    }
    data->sample_rate = config->sample_rate;
    data->channels = config->channels;
    SDL_PauseAudioDevice(data->device_id, 0);
    return ERROR_NONE;
}

error_t close(Device* device) {
    auto* data = GET_DATA(device);
    if (data->device_id != 0) {
        uint32_t waited_ms = 0;
        while (SDL_GetQueuedAudioSize(data->device_id) > 0 && waited_ms < MAX_DRAIN_MS) {
            // A FreeRTOS delay, as a plain sleep would keep the other tasks from running
            delay_millis(POLL_MS);
            waited_ms += POLL_MS;
        }
        SDL_CloseAudioDevice(data->device_id);
        data->device_id = 0;
    }
    return ERROR_NONE;
}

error_t read(Device*, void*, size_t, size_t*, TickType_t) {
    return ERROR_NOT_SUPPORTED;
}

error_t write(Device* device, const void* buffer, size_t size, size_t* bytes_written, TickType_t timeout) {
    auto* data = GET_DATA(device);
    *bytes_written = 0;
    if (data->device_id == 0) {
        return ERROR_RESOURCE;
    }

    const uint32_t bytes_per_ms = data->sample_rate * data->channels * sizeof(int16_t) / 1000;
    const uint32_t max_queued = bytes_per_ms * MAX_QUEUED_MS;
    const bool wait_forever = timeout == portMAX_DELAY;
    const uint32_t timeout_ms = static_cast<uint32_t>(timeout) * portTICK_PERIOD_MS;
    uint32_t waited_ms = 0;
    while (SDL_GetQueuedAudioSize(data->device_id) > max_queued) {
        if (!wait_forever && waited_ms >= timeout_ms) {
            return ERROR_TIMEOUT;
        }
        // A FreeRTOS delay, as a plain sleep would keep the other tasks from running
        delay_millis(POLL_MS);
        waited_ms += POLL_MS;
    }

    const size_t samples = size / sizeof(int16_t);
    const float gain = data->muted ? 0.0f : data->volume_percent / 100.0f;
    std::vector<int16_t> scaled(samples);
    const auto* input = static_cast<const int16_t*>(buffer);
    for (size_t i = 0; i < samples; i++) {
        scaled[i] = static_cast<int16_t>(input[i] * gain);
    }
    if (SDL_QueueAudio(data->device_id, scaled.data(), static_cast<Uint32>(samples * sizeof(int16_t))) != 0) {
        LOG_E(TAG, "SDL_QueueAudio failed: %s", SDL_GetError());
        return ERROR_RESOURCE;
    }
    *bytes_written = samples * sizeof(int16_t);
    return ERROR_NONE;
}

error_t set_volume(Device* device, AudioCodecDirection direction, float volume_percent) {
    if (direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }
    if (volume_percent < 0.0f || volume_percent > 100.0f) {
        return ERROR_INVALID_ARGUMENT;
    }
    GET_DATA(device)->volume_percent = volume_percent;
    return ERROR_NONE;
}

error_t get_volume(Device* device, AudioCodecDirection direction, float* volume_percent) {
    if (direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }
    *volume_percent = GET_DATA(device)->volume_percent;
    return ERROR_NONE;
}

error_t set_mute(Device* device, AudioCodecDirection direction, bool muted) {
    if (direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }
    GET_DATA(device)->muted = muted;
    return ERROR_NONE;
}

error_t get_mute(Device* device, AudioCodecDirection direction, bool* muted) {
    if (direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }
    *muted = GET_DATA(device)->muted;
    return ERROR_NONE;
}

error_t get_native_sample_rate(Device*, AudioCodecDirection direction, uint32_t* rate_hz) {
    if (direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }
    *rate_hz = NATIVE_SAMPLE_RATE;
    return ERROR_NONE;
}

error_t get_native_channels(Device*, AudioCodecDirection direction, uint8_t* channels) {
    if (direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }
    *channels = NATIVE_CHANNELS;
    return ERROR_NONE;
}

error_t get_capabilities(Device*, AudioCodecDirection* supported_directions) {
    *supported_directions = AUDIO_CODEC_DIR_OUTPUT;
    return ERROR_NONE;
}

const AudioCodecApi api = {
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
    .get_input_gain_multiplier = nullptr,
};

// Fails when the machine has no audio output, so the audio service doesn't offer sound that can't play
error_t start(Device* device) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        LOG_W(TAG, "No audio: %s", SDL_GetError());
        return ERROR_NOT_SUPPORTED;
    }
    // SDL_GetNumAudioDevices() can report 0 on systems where the default device still opens, so the device is tried
    SDL_AudioSpec wanted = {};
    wanted.freq = NATIVE_SAMPLE_RATE;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = NATIVE_CHANNELS;
    wanted.samples = 1024;
    const SDL_AudioDeviceID probe = SDL_OpenAudioDevice(nullptr, 0, &wanted, nullptr, 0);
    if (probe == 0) {
        LOG_W(TAG, "No audio output device: %s", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return ERROR_NOT_SUPPORTED;
    }
    SDL_CloseAudioDevice(probe);
    auto* data = new(std::nothrow) SdlAudioInternal();
    if (data == nullptr) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return ERROR_OUT_OF_MEMORY;
    }
    device_set_driver_data(device, data);
    return ERROR_NONE;
}

error_t stop(Device* device) {
    auto* data = GET_DATA(device);
    if (data->device_id != 0) {
        SDL_CloseAudioDevice(data->device_id);
    }
    delete data;
    device_set_driver_data(device, nullptr);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return ERROR_NONE;
}

} // namespace

Driver sdl_audio_driver = {
    .name = "sdl-audio",
    .compatible = (const char*[]) { "tactility,sdl-audio", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = &api,
    .device_type = &AUDIO_CODEC_TYPE,
    .internal = nullptr,
};
