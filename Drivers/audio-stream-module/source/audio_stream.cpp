// SPDX-License-Identifier: Apache-2.0
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/log.h>
#include <tactility/drivers/audio_codec.h>
#include <tactility/drivers/audio_stream.h>

#include <cstring>
#include <vector>

#define TAG "AudioStream"

namespace {

// Linear-interpolation resampler. Cheap and good enough for voice/UI audio; S3/P4 have
// plenty of headroom for this at the rates this subsystem targets (16k/44.1k/48k).
// Operates on interleaved 16-bit PCM, which is what esp_codec_dev / our codec drivers use.
size_t resample_s16(const int16_t* in, size_t in_frames, uint8_t channels,
                   uint32_t in_rate, uint32_t out_rate,
                   int16_t* out, size_t out_frame_capacity) {
    if (in_rate == out_rate) {
        size_t frames = (in_frames < out_frame_capacity) ? in_frames : out_frame_capacity;
        std::memcpy(out, in, frames * channels * sizeof(int16_t));
        return frames;
    }

    if (in_frames == 0) {
        return 0;
    }

    double ratio = (double) in_rate / (double) out_rate;
    size_t out_frames = 0;
    for (; out_frames < out_frame_capacity; out_frames++) {
        double src_pos = (double) out_frames * ratio;
        size_t src_index = (size_t) src_pos;
        if (src_index + 1 >= in_frames) {
            if (src_index >= in_frames) {
                break;
            }
            // Last frame: no next sample to interpolate with, repeat it.
            for (uint8_t channel = 0; channel < channels; channel++) {
                out[out_frames * channels + channel] = in[src_index * channels + channel];
            }
            continue;
        }

        double frac = src_pos - (double) src_index;
        for (uint8_t channel = 0; channel < channels; channel++) {
            int16_t a = in[src_index * channels + channel];
            int16_t b = in[(src_index + 1) * channels + channel];
            out[out_frames * channels + channel] = (int16_t) ((double) a + ((double) b - (double) a) * frac);
        }
    }

    return out_frames;
}

// Converts between interleaved S16 PCM with different channel counts.
// - channels_out < channels_in: downmix by averaging the first `channels_out` source channels
//   plus folding any extra source channels into them round-robin (e.g. 4 -> 1 averages all 4;
//   4 -> 2 averages {0,2} into channel 0 and {1,3} into channel 1).
// - channels_out > channels_in: upmix by repeating source channels round-robin (e.g. mono -> stereo
//   duplicates the single channel into both output channels).
// - equal: copies through.
void convert_channels_s16(const int16_t* in, size_t frames, uint8_t channels_in,
                        int16_t* out, uint8_t channels_out) {
    if (channels_in == channels_out) {
        std::memcpy(out, in, frames * channels_in * sizeof(int16_t));
        return;
    }

    for (size_t frame = 0; frame < frames; frame++) {
        const int16_t* in_frame = in + frame * channels_in;
        int16_t* out_frame = out + frame * channels_out;

        if (channels_out < channels_in) {
            for (uint8_t out_ch = 0; out_ch < channels_out; out_ch++) {
                int32_t sum = 0;
                uint8_t count = 0;
                for (uint8_t in_ch = out_ch; in_ch < channels_in; in_ch += channels_out) {
                    sum += in_frame[in_ch];
                    count++;
                }
                out_frame[out_ch] = (int16_t) (sum / (int32_t) count);
            }
        } else {
            for (uint8_t out_ch = 0; out_ch < channels_out; out_ch++) {
                out_frame[out_ch] = in_frame[out_ch % channels_in];
            }
        }
    }
}

struct AudioStreamHandleImpl : AudioStreamHandleData {
    AudioCodecDirection direction = AUDIO_CODEC_DIR_BOTH;
    struct AudioStreamConfig config = {};
    uint32_t codec_rate = 0;
    uint8_t codec_channels = 0;
    uint8_t bytes_per_frame = 0;     // app-side frame size (config.channels)
    uint8_t codec_bytes_per_frame = 0; // codec-side frame size (codec_channels)
    float input_gain = 1.0f; // fixed digital gain multiplier, input direction only (see audio_codec_get_input_gain_multiplier)
    std::vector<uint8_t> codec_buffer;    // raw codec-rate/codec-channel PCM, scratch
    std::vector<uint8_t> convert_buffer;  // intermediate scratch for the second conversion stage

    // Lifetime guard: close_stream() can be triggered from a different task than the one
    // doing read()/write() (e.g. the Settings UI disabling output while SfxEngine's audio
    // task is mid-write). `closing` keeps new I/O calls out, `busy_count` tracks I/O calls
    // currently in flight, and `drain_semaphore` lets close_stream() block until they finish
    // before freeing the handle. All three are only touched while `AudioStreamData::mutex`
    // is held, except for the give/take on drain_semaphore itself.
    bool closing = false;
    int busy_count = 0;
    SemaphoreHandle_t drain_semaphore = nullptr;
};

struct AudioStreamData {
    Device* input_codec = nullptr;
    Device* output_codec = nullptr;
    bool input_enabled = true;
    bool output_enabled = true;

    // Codecs reject volume/mute/gain calls until their chip is initialized, which only
    // happens when a stream of that direction is first opened (audio_codec_open ->
    // esp_codec_dev_open -> chip's open()/enable()). A Settings UI must be able to set
    // these regardless of whether anything is currently streaming, so we cache the desired
    // values here, apply them best-effort immediately, and replay them once the codec opens.
    float input_volume = 100.0f;
    float output_volume = 100.0f;
    bool input_muted = false;
    bool output_muted = false;
    AudioStreamHandleImpl* open_input = nullptr;
    AudioStreamHandleImpl* open_output = nullptr;
    // Guards open_input/open_output and the closing/busy_count fields of any handle reachable
    // through them, so close (possibly forced by set_enabled) can't race with read/write.
    SemaphoreHandle_t mutex = nullptr;

    AudioStreamChangeCallback change_callback = nullptr;
    void* change_callback_user_data = nullptr;
};

// Reads the registered change callback under the lock, then invokes it outside the lock
// (same lock-then-release-then-act pattern as codec_for_direction).
void notify_change(AudioStreamData* data, Device* device, AudioCodecDirection direction, AudioStreamChange change) {
    xSemaphoreTake(data->mutex, portMAX_DELAY);
    AudioStreamChangeCallback callback = data->change_callback;
    void* user_data = data->change_callback_user_data;
    xSemaphoreGive(data->mutex);

    if (callback != nullptr) {
        callback(device, direction, change, user_data);
    }
}

#define GET_DATA(device) (static_cast<AudioStreamData*>(device_get_driver_data(device)))

struct CodecSearchContext {
    AudioCodecDirection wanted_direction;
    Device* exact_match = nullptr;
    Device* fallback_match = nullptr;
};

bool find_codec_by_direction(Device* device, void* context_ptr) {
    auto* context = static_cast<CodecSearchContext*>(context_ptr);
    if (!device_is_ready(device)) {
        return true; // continue searching
    }

    AudioCodecDirection capabilities = AUDIO_CODEC_DIR_BOTH;
    if (audio_codec_get_capabilities(device, &capabilities) != ERROR_NONE) {
        return true;
    }

    if (capabilities == context->wanted_direction) {
        // Dedicated codec for exactly this direction (e.g. an input-only mic ADC) --
        // prefer it over a wider-capability codec and stop looking.
        context->exact_match = device;
        return false;
    }

    if ((capabilities & context->wanted_direction) == context->wanted_direction
        && context->fallback_match == nullptr) {
        // Supports the wanted direction as part of a wider capability set (e.g. a
        // BOTH-capable codec asked for INPUT). Remember it but keep looking -- boards
        // commonly pair a BOTH-capable output codec with a separate dedicated input
        // ADC, and binding the wrong one makes both directions fight over the same
        // physical esp_codec_dev handle (reopen for input reconfigures/breaks output).
        context->fallback_match = device;
    }

    return true;
}

Device* find_first_codec_supporting(AudioCodecDirection direction) {
    CodecSearchContext context = { .wanted_direction = direction };
    device_for_each_of_type(&AUDIO_CODEC_TYPE, &context, find_codec_by_direction);
    return (context.exact_match != nullptr) ? context.exact_match : context.fallback_match;
}

// The audio-stream device is constructed while modules start, which happens before the
// device tree's codec devices (nested under i2c0) are started. So codecs can't be resolved
// at start_device time — resolve (and cache) them lazily on first use instead, by which
// point the device tree has finished starting.
Device* codec_for_direction(AudioStreamData* data, AudioCodecDirection direction) {
    Device** slot = (direction == AUDIO_CODEC_DIR_INPUT) ? &data->input_codec : &data->output_codec;

    // Fast path: already resolved (the common case after first use).
    Device* existing = *slot;
    if (existing != nullptr) {
        return existing;
    }

    // Slow path: resolve outside the lock (device_for_each_of_type can be costly), then
    // re-check under the mutex before committing -- avoids two threads racing to write
    // *slot (and double-logging "Bound ... codec").
    Device* found = find_first_codec_supporting(direction);

    xSemaphoreTake(data->mutex, portMAX_DELAY);
    if (*slot == nullptr) {
        *slot = found;
        if (found != nullptr) {
            LOG_I(TAG, "Bound %s codec: %s", (direction == AUDIO_CODEC_DIR_INPUT) ? "input" : "output", found->name);
        }
    }
    Device* result = *slot;
    xSemaphoreGive(data->mutex);
    return result;
}

// Marks an I/O operation as in-flight on `handle`, preventing close_stream() from freeing it
// underneath us. Returns false (and does nothing further) if the handle is closing/closed --
// callers must bail out with an error in that case. Must be paired with io_end().
bool io_begin(AudioStreamData* data, AudioStreamHandleImpl* handle) {
    xSemaphoreTake(data->mutex, portMAX_DELAY);
    bool is_input = (handle->direction == AUDIO_CODEC_DIR_INPUT);
    AudioStreamHandleImpl* slot_value = is_input ? data->open_input : data->open_output;
    if (slot_value != handle || handle->closing) {
        xSemaphoreGive(data->mutex);
        return false;
    }
    handle->busy_count++;
    xSemaphoreGive(data->mutex);
    return true;
}

void io_end(AudioStreamData* data, AudioStreamHandleImpl* handle) {
    xSemaphoreTake(data->mutex, portMAX_DELAY);
    handle->busy_count--;
    if (handle->closing && handle->busy_count == 0) {
        xSemaphoreGive(handle->drain_semaphore);
    }
    xSemaphoreGive(data->mutex);
}

// region AudioStreamApi

error_t open_stream(Device* device, const struct AudioStreamConfig* config, AudioCodecDirection direction, AudioStreamHandle* out_handle) {
    if (config->bits_per_sample != 8 && config->bits_per_sample != 16
        && config->bits_per_sample != 24 && config->bits_per_sample != 32) {
        // bytes_per_frame/codec_bytes_per_frame below assume a whole number of bytes per
        // sample; anything else corrupts every frame-size calculation in read/write_stream.
        return ERROR_INVALID_ARGUMENT;
    }

    if (config->channels == 0) {
        return ERROR_INVALID_ARGUMENT;
    }

    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_RESOURCE;
    }

    bool is_input = (direction == AUDIO_CODEC_DIR_INPUT);
    Device* codec = codec_for_direction(data, direction);
    if (codec == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }

    xSemaphoreTake(data->mutex, portMAX_DELAY);

    if ((is_input && !data->input_enabled) || (!is_input && !data->output_enabled)) {
        xSemaphoreGive(data->mutex);
        return ERROR_NOT_ALLOWED;
    }

    AudioStreamHandleImpl** slot = is_input ? &data->open_input : &data->open_output;
    if (*slot != nullptr) {
        xSemaphoreGive(data->mutex);
        return ERROR_INVALID_STATE;
    }

    // Reserve the slot with a placeholder so concurrent opens can't race past the check
    // above while we do the (potentially slow) codec open below outside the lock.
    auto* reservation = reinterpret_cast<AudioStreamHandleImpl*>(1);
    *slot = reservation;
    xSemaphoreGive(data->mutex);

    uint32_t codec_rate = 0;
    if (audio_codec_get_native_sample_rate(codec, direction, &codec_rate) != ERROR_NONE || codec_rate == 0) {
        xSemaphoreTake(data->mutex, portMAX_DELAY);
        if (*slot == reservation) { *slot = nullptr; }
        xSemaphoreGive(data->mutex);
        return ERROR_RESOURCE;
    }

    // The codec must be opened with its native channel layout (e.g. 4 for a 4-slot TDM mic
    // ADC) -- opening it with the app's requested channel count can silently corrupt the
    // stream (ES7210 in TDM mode halves its configured bit depth for <= 2 channels). We
    // convert between the codec's layout and the app's requested channel count ourselves.
    uint8_t codec_channels = config->channels;
    if (audio_codec_get_native_channels(codec, direction, &codec_channels) != ERROR_NONE || codec_channels == 0) {
        codec_channels = config->channels;
    }

    struct AudioCodecStreamConfig codec_config = {
        .sample_rate = codec_rate,
        .bits_per_sample = config->bits_per_sample,
        .channels = codec_channels,
        .direction = direction,
    };

    if (audio_codec_open(codec, &codec_config) != ERROR_NONE) {
        LOG_E(TAG, "Failed to open codec for %s", is_input ? "input" : "output");
        xSemaphoreTake(data->mutex, portMAX_DELAY);
        if (*slot == reservation) { *slot = nullptr; }
        xSemaphoreGive(data->mutex);
        return ERROR_RESOURCE;
    }

    // The chip is initialized now -- replay any volume/mute settings that were requested
    // before this stream existed (set_volume/set_mute cache them but the chip rejects them
    // until opened).
    if (is_input) {
        audio_codec_set_volume(codec, AUDIO_CODEC_DIR_INPUT, data->input_volume);
        audio_codec_set_mute(codec, AUDIO_CODEC_DIR_INPUT, data->input_muted);
    } else {
        audio_codec_set_volume(codec, AUDIO_CODEC_DIR_OUTPUT, data->output_volume);
        audio_codec_set_mute(codec, AUDIO_CODEC_DIR_OUTPUT, data->output_muted);
    }

    auto* handle = new AudioStreamHandleImpl();
    handle->device = device;
    handle->direction = direction;
    handle->config = *config;
    handle->codec_rate = codec_rate;
    handle->codec_channels = codec_channels;
    handle->bytes_per_frame = (uint8_t) ((config->bits_per_sample / 8) * config->channels);
    handle->codec_bytes_per_frame = (uint8_t) ((config->bits_per_sample / 8) * codec_channels);
    if (is_input) {
        audio_codec_get_input_gain_multiplier(codec, &handle->input_gain);
    }
    handle->drain_semaphore = xSemaphoreCreateBinary();
    if (handle->drain_semaphore == nullptr) {
        LOG_E(TAG, "Failed to create drain semaphore");
        delete handle;
        audio_codec_close(codec);
        xSemaphoreTake(data->mutex, portMAX_DELAY);
        if (*slot == reservation) { *slot = nullptr; }
        xSemaphoreGive(data->mutex);
        return ERROR_OUT_OF_MEMORY;
    }

    xSemaphoreTake(data->mutex, portMAX_DELAY);
    *slot = handle;
    xSemaphoreGive(data->mutex);

    *out_handle = handle;
    return ERROR_NONE;
}

error_t open_input(Device* device, const struct AudioStreamConfig* config, AudioStreamHandle* out_handle) {
    return open_stream(device, config, AUDIO_CODEC_DIR_INPUT, out_handle);
}

error_t open_output(Device* device, const struct AudioStreamConfig* config, AudioStreamHandle* out_handle) {
    return open_stream(device, config, AUDIO_CODEC_DIR_OUTPUT, out_handle);
}

error_t read_stream(AudioStreamHandle handle_base, void* out_data, size_t data_size, size_t* bytes_read, TickType_t timeout) {
    auto* handle = static_cast<AudioStreamHandleImpl*>(handle_base);
    if (handle->direction != AUDIO_CODEC_DIR_INPUT || handle->bytes_per_frame == 0) {
        return ERROR_INVALID_STATE;
    }

    auto* data = GET_DATA(handle->device);
    if (data == nullptr || data->input_codec == nullptr) {
        return ERROR_RESOURCE;
    }

    size_t requested_frames = data_size / handle->bytes_per_frame;
    if (requested_frames == 0) {
        if (bytes_read != nullptr) {
            *bytes_read = 0;
        }
        return ERROR_NONE;
    }

    if (!io_begin(data, handle)) {
        return ERROR_INVALID_STATE;
    }

    bool same_rate = (handle->codec_rate == handle->config.sample_rate);
    bool same_channels = (handle->codec_channels == handle->config.channels);

    error_t result;
    if (same_rate && same_channels) {
        size_t codec_bytes_read = 0;
        result = audio_codec_read(data->input_codec, out_data, data_size, &codec_bytes_read, timeout);
        if (bytes_read != nullptr) {
            *bytes_read = codec_bytes_read;
        }
    } else {
        // Read enough codec-rate frames to produce the requested number of output-rate frames.
        size_t codec_frames = (size_t) ((double) requested_frames * ((double) handle->codec_rate / (double) handle->config.sample_rate)) + 2;
        size_t codec_bytes_needed = codec_frames * handle->codec_bytes_per_frame;
        if (handle->codec_buffer.size() < codec_bytes_needed) {
            handle->codec_buffer.resize(codec_bytes_needed);
        }

        size_t codec_bytes_read = 0;
        result = audio_codec_read(data->input_codec, handle->codec_buffer.data(), codec_bytes_needed, &codec_bytes_read, timeout);
        if (result == ERROR_NONE) {
            size_t codec_frames_read = codec_bytes_read / handle->codec_bytes_per_frame;
            const int16_t* rate_input = reinterpret_cast<const int16_t*>(handle->codec_buffer.data());
            uint8_t rate_input_channels = handle->codec_channels;
            size_t rate_input_frames = codec_frames_read;

            // Downmix first (while still at the codec's higher rate -- cheaper) if needed.
            if (!same_channels) {
                size_t convert_bytes_needed = codec_frames_read * handle->bytes_per_frame;
                if (handle->convert_buffer.size() < convert_bytes_needed) {
                    handle->convert_buffer.resize(convert_bytes_needed);
                }
                convert_channels_s16(rate_input, codec_frames_read, handle->codec_channels,
                                   reinterpret_cast<int16_t*>(handle->convert_buffer.data()), handle->config.channels);
                rate_input = reinterpret_cast<const int16_t*>(handle->convert_buffer.data());
                rate_input_channels = handle->config.channels;
            }

            size_t out_frames = resample_s16(
                rate_input, rate_input_frames, rate_input_channels,
                handle->codec_rate, handle->config.sample_rate,
                reinterpret_cast<int16_t*>(out_data), requested_frames);

            if (bytes_read != nullptr) {
                *bytes_read = out_frames * handle->bytes_per_frame;
            }
        }
    }

    if (result == ERROR_NONE && handle->input_gain != 1.0f && bytes_read != nullptr && *bytes_read > 0) {
        auto* samples = reinterpret_cast<int16_t*>(out_data);
        size_t sample_count = *bytes_read / sizeof(int16_t);
        for (size_t i = 0; i < sample_count; i++) {
            float boosted = (float) samples[i] * handle->input_gain;
            samples[i] = (int16_t) (boosted < -32768.0f ? -32768.0f : boosted > 32767.0f ? 32767.0f : boosted);
        }
    }

    io_end(data, handle);
    return result;
}

error_t write_stream(AudioStreamHandle handle_base, const void* in_data, size_t data_size, size_t* bytes_written, TickType_t timeout) {
    auto* handle = static_cast<AudioStreamHandleImpl*>(handle_base);
    if (handle->direction != AUDIO_CODEC_DIR_OUTPUT || handle->bytes_per_frame == 0) {
        return ERROR_INVALID_STATE;
    }

    auto* data = GET_DATA(handle->device);
    if (data == nullptr || data->output_codec == nullptr) {
        return ERROR_RESOURCE;
    }

    size_t in_frames = data_size / handle->bytes_per_frame;
    if (in_frames == 0) {
        if (bytes_written != nullptr) {
            *bytes_written = 0;
        }
        return ERROR_NONE;
    }

    if (!io_begin(data, handle)) {
        return ERROR_INVALID_STATE;
    }

    bool same_rate = (handle->codec_rate == handle->config.sample_rate);
    bool same_channels = (handle->codec_channels == handle->config.channels);

    error_t result;
    if (same_rate && same_channels) {
        size_t codec_bytes_written = 0;
        result = audio_codec_write(data->output_codec, in_data, data_size, &codec_bytes_written, timeout);
        if (bytes_written != nullptr) {
            // Report in terms of input bytes consumed, matching codec_bytes_written 1:1 here.
            *bytes_written = codec_bytes_written;
        }
    } else {
        const int16_t* rate_input = reinterpret_cast<const int16_t*>(in_data);
        uint8_t rate_input_channels = handle->config.channels;
        size_t rate_input_frames = in_frames;

        // Upmix/downmix first (while still at the app's rate -- cheaper if downmixing) if needed.
        if (!same_channels) {
            size_t convert_bytes_needed = in_frames * handle->codec_bytes_per_frame;
            if (handle->convert_buffer.size() < convert_bytes_needed) {
                handle->convert_buffer.resize(convert_bytes_needed);
            }
            convert_channels_s16(rate_input, in_frames, handle->config.channels,
                               reinterpret_cast<int16_t*>(handle->convert_buffer.data()), handle->codec_channels);
            rate_input = reinterpret_cast<const int16_t*>(handle->convert_buffer.data());
            rate_input_channels = handle->codec_channels;
        }

        size_t codec_frame_capacity = (size_t) ((double) rate_input_frames * ((double) handle->codec_rate / (double) handle->config.sample_rate)) + 2;
        size_t codec_bytes_capacity = codec_frame_capacity * handle->codec_bytes_per_frame;
        if (handle->codec_buffer.size() < codec_bytes_capacity) {
            handle->codec_buffer.resize(codec_bytes_capacity);
        }

        size_t codec_frames;
        if (same_rate) {
            codec_frames = rate_input_frames;
            std::memcpy(handle->codec_buffer.data(), rate_input, rate_input_frames * handle->codec_bytes_per_frame);
        } else {
            codec_frames = resample_s16(
                rate_input, rate_input_frames, rate_input_channels,
                handle->config.sample_rate, handle->codec_rate,
                reinterpret_cast<int16_t*>(handle->codec_buffer.data()), codec_frame_capacity);
        }

        size_t codec_bytes_to_write = codec_frames * handle->codec_bytes_per_frame;
        size_t codec_bytes_written = 0;
        result = audio_codec_write(data->output_codec, handle->codec_buffer.data(), codec_bytes_to_write, &codec_bytes_written, timeout);
        if (result == ERROR_NONE && bytes_written != nullptr) {
            // The caller provided `data_size` worth of input; we consumed all of it (resampled/converted).
            *bytes_written = data_size;
        }
    }

    io_end(data, handle);
    return result;
}

error_t close_stream(AudioStreamHandle handle_base) {
    auto* handle = static_cast<AudioStreamHandleImpl*>(handle_base);
    auto* data = GET_DATA(handle->device);
    if (data == nullptr) {
        return ERROR_RESOURCE;
    }

    bool is_input = (handle->direction == AUDIO_CODEC_DIR_INPUT);
    Device* codec = is_input ? data->input_codec : data->output_codec;
    AudioStreamHandleImpl** slot = is_input ? &data->open_input : &data->open_output;

    xSemaphoreTake(data->mutex, portMAX_DELAY);
    if (handle->closing) {
        // Already being closed by another caller (e.g. concurrent set_enabled + app close).
        xSemaphoreGive(data->mutex);
        return ERROR_NONE;
    }
    handle->closing = true;
    if (*slot == handle) {
        *slot = nullptr;
    }
    bool must_drain = (handle->busy_count > 0);
    xSemaphoreGive(data->mutex);

    if (must_drain && handle->drain_semaphore != nullptr) {
        xSemaphoreTake(handle->drain_semaphore, portMAX_DELAY);
    }

    if (codec != nullptr) {
        audio_codec_close(codec);
    }

    if (handle->drain_semaphore != nullptr) {
        vSemaphoreDelete(handle->drain_semaphore);
    }

    delete handle;
    return ERROR_NONE;
}

error_t set_volume(Device* device, AudioCodecDirection direction, float volume_percent) {
    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_RESOURCE;
    }
    Device* codec = codec_for_direction(data, direction);
    if (codec == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }

    bool is_input = (direction == AUDIO_CODEC_DIR_INPUT);
    xSemaphoreTake(data->mutex, portMAX_DELAY);
    if (is_input) { data->input_volume = volume_percent; } else { data->output_volume = volume_percent; }
    xSemaphoreGive(data->mutex);

    // The chip rejects this until it's been opened by a stream of this direction; that's
    // fine -- the cached value above gets replayed in open_stream() once it is.
    audio_codec_set_volume(codec, direction, volume_percent);
    notify_change(data, device, direction, AUDIO_STREAM_CHANGE_VOLUME);
    return ERROR_NONE;
}

error_t get_volume(Device* device, AudioCodecDirection direction, float* volume_percent) {
    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_RESOURCE;
    }
    Device* codec = codec_for_direction(data, direction);
    if (codec == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }

    if (audio_codec_get_volume(codec, direction, volume_percent) == ERROR_NONE) {
        return ERROR_NONE;
    }

    // Codec not open yet -- report the cached/desired value instead.
    xSemaphoreTake(data->mutex, portMAX_DELAY);
    *volume_percent = (direction == AUDIO_CODEC_DIR_INPUT) ? data->input_volume : data->output_volume;
    xSemaphoreGive(data->mutex);
    return ERROR_NONE;
}

error_t set_mute(Device* device, AudioCodecDirection direction, bool muted) {
    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_RESOURCE;
    }
    Device* codec = codec_for_direction(data, direction);
    if (codec == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }

    bool is_input = (direction == AUDIO_CODEC_DIR_INPUT);
    xSemaphoreTake(data->mutex, portMAX_DELAY);
    if (is_input) { data->input_muted = muted; } else { data->output_muted = muted; }
    xSemaphoreGive(data->mutex);

    // As with volume, the chip may reject this until opened; cached value is replayed
    // in open_stream().
    audio_codec_set_mute(codec, direction, muted);
    notify_change(data, device, direction, AUDIO_STREAM_CHANGE_MUTE);
    return ERROR_NONE;
}

error_t get_mute(Device* device, AudioCodecDirection direction, bool* muted) {
    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_RESOURCE;
    }
    Device* codec = codec_for_direction(data, direction);
    if (codec == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }

    if (audio_codec_get_mute(codec, direction, muted) == ERROR_NONE) {
        return ERROR_NONE;
    }

    xSemaphoreTake(data->mutex, portMAX_DELAY);
    *muted = (direction == AUDIO_CODEC_DIR_INPUT) ? data->input_muted : data->output_muted;
    xSemaphoreGive(data->mutex);
    return ERROR_NONE;
}

error_t set_enabled(Device* device, AudioCodecDirection direction, bool enabled) {
    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_RESOURCE;
    }

    bool is_input = (direction == AUDIO_CODEC_DIR_INPUT);
    Device* codec = codec_for_direction(data, direction);
    if (codec == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }

    xSemaphoreTake(data->mutex, portMAX_DELAY);
    if (is_input) {
        data->input_enabled = enabled;
    } else {
        data->output_enabled = enabled;
    }

    // Capture and clear the slot under the lock so we hand close_stream() a pointer that
    // can't simultaneously be torn down by a racing close from the owning app (close_stream
    // re-checks `*slot == handle` and no-ops if it's already been cleared/replaced).
    AudioStreamHandleImpl* to_close = nullptr;
    if (!enabled) {
        AudioStreamHandleImpl** slot = is_input ? &data->open_input : &data->open_output;
        to_close = *slot;
    }
    xSemaphoreGive(data->mutex);

    if (to_close != nullptr) {
        close_stream(to_close);
    }

    notify_change(data, device, direction, AUDIO_STREAM_CHANGE_ENABLED);
    return ERROR_NONE;
}

error_t get_enabled(Device* device, AudioCodecDirection direction, bool* enabled) {
    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_RESOURCE;
    }
    Device* codec = codec_for_direction(data, direction);
    if (codec == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    xSemaphoreTake(data->mutex, portMAX_DELAY);
    *enabled = (direction == AUDIO_CODEC_DIR_INPUT) ? data->input_enabled : data->output_enabled;
    xSemaphoreGive(data->mutex);
    return ERROR_NONE;
}

error_t is_supported(Device* device, AudioCodecDirection direction, bool* supported) {
    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_RESOURCE;
    }
    *supported = codec_for_direction(data, direction) != nullptr;
    return ERROR_NONE;
}

error_t set_change_callback(Device* device, AudioStreamChangeCallback callback, void* user_data) {
    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_RESOURCE;
    }

    xSemaphoreTake(data->mutex, portMAX_DELAY);
    data->change_callback = callback;
    data->change_callback_user_data = user_data;
    xSemaphoreGive(data->mutex);
    return ERROR_NONE;
}

static const struct AudioStreamApi API = {
    .open_input = open_input,
    .open_output = open_output,
    .read = read_stream,
    .write = write_stream,
    .close = close_stream,
    .set_volume = set_volume,
    .get_volume = get_volume,
    .set_mute = set_mute,
    .get_mute = get_mute,
    .set_enabled = set_enabled,
    .get_enabled = get_enabled,
    .is_supported = is_supported,
    .set_change_callback = set_change_callback,
};

// endregion

// region Driver lifecycle

error_t start_device(Device* device) {
    auto* data = new AudioStreamData();
    data->mutex = xSemaphoreCreateMutex();
    if (data->mutex == nullptr) {
        delete data;
        return ERROR_OUT_OF_MEMORY;
    }
    device_set_driver_data(device, data);
    return ERROR_NONE;
}

error_t stop_device(Device* device) {
    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_NONE;
    }

    if (data->open_input != nullptr) {
        close_stream(data->open_input);
    }
    if (data->open_output != nullptr) {
        close_stream(data->open_output);
    }

    device_set_driver_data(device, nullptr);
    if (data->mutex != nullptr) {
        vSemaphoreDelete(data->mutex);
    }
    delete data;
    return ERROR_NONE;
}

// endregion

} // namespace

extern "C" {

Driver audio_stream_driver = {
    .name = "audio-stream",
    .compatible = (const char*[]) { "audio-stream", nullptr },
    .start_device = start_device,
    .stop_device = stop_device,
    .api = &API,
    .device_type = &AUDIO_STREAM_TYPE,
    .owner = nullptr,
    .internal = nullptr,
};

Device audio_stream_device = {
    .address = 0,
    .name = "audio-stream0",
    .config = nullptr,
    .parent = nullptr,
    .internal = nullptr,
};

}
