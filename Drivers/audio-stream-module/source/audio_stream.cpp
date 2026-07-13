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
size_t resampleS16(const int16_t* in, size_t inFrames, uint8_t channels,
                   uint32_t inRate, uint32_t outRate,
                   int16_t* out, size_t outFrameCapacity) {
    if (inRate == outRate) {
        size_t frames = (inFrames < outFrameCapacity) ? inFrames : outFrameCapacity;
        std::memcpy(out, in, frames * channels * sizeof(int16_t));
        return frames;
    }

    if (inFrames == 0) {
        return 0;
    }

    double ratio = (double) inRate / (double) outRate;
    size_t outFrames = 0;
    for (; outFrames < outFrameCapacity; outFrames++) {
        double srcPos = (double) outFrames * ratio;
        size_t srcIndex = (size_t) srcPos;
        if (srcIndex + 1 >= inFrames) {
            if (srcIndex >= inFrames) {
                break;
            }
            // Last frame: no next sample to interpolate with, repeat it.
            for (uint8_t channel = 0; channel < channels; channel++) {
                out[outFrames * channels + channel] = in[srcIndex * channels + channel];
            }
            continue;
        }

        double frac = srcPos - (double) srcIndex;
        for (uint8_t channel = 0; channel < channels; channel++) {
            int16_t a = in[srcIndex * channels + channel];
            int16_t b = in[(srcIndex + 1) * channels + channel];
            out[outFrames * channels + channel] = (int16_t) ((double) a + ((double) b - (double) a) * frac);
        }
    }

    return outFrames;
}

// Converts between interleaved S16 PCM with different channel counts.
// - channelsOut < channelsIn: downmix by averaging the first `channelsOut` source channels
//   plus folding any extra source channels into them round-robin (e.g. 4 -> 1 averages all 4;
//   4 -> 2 averages {0,2} into channel 0 and {1,3} into channel 1).
// - channelsOut > channelsIn: upmix by repeating source channels round-robin (e.g. mono -> stereo
//   duplicates the single channel into both output channels).
// - equal: copies through.
void convertChannelsS16(const int16_t* in, size_t frames, uint8_t channelsIn,
                        int16_t* out, uint8_t channelsOut) {
    if (channelsIn == channelsOut) {
        std::memcpy(out, in, frames * channelsIn * sizeof(int16_t));
        return;
    }

    for (size_t frame = 0; frame < frames; frame++) {
        const int16_t* inFrame = in + frame * channelsIn;
        int16_t* outFrame = out + frame * channelsOut;

        if (channelsOut < channelsIn) {
            for (uint8_t outCh = 0; outCh < channelsOut; outCh++) {
                int32_t sum = 0;
                uint8_t count = 0;
                for (uint8_t inCh = outCh; inCh < channelsIn; inCh += channelsOut) {
                    sum += inFrame[inCh];
                    count++;
                }
                outFrame[outCh] = (int16_t) (sum / (int32_t) count);
            }
        } else {
            for (uint8_t outCh = 0; outCh < channelsOut; outCh++) {
                outFrame[outCh] = inFrame[outCh % channelsIn];
            }
        }
    }
}

struct AudioStreamHandleImpl : AudioStreamHandleData {
    enum AudioCodecDirection direction = AUDIO_CODEC_DIR_BOTH;
    struct AudioStreamConfig config = {};
    uint32_t codecRate = 0;
    uint8_t codecChannels = 0;
    uint8_t bytesPerFrame = 0;     // app-side frame size (config.channels)
    uint8_t codecBytesPerFrame = 0; // codec-side frame size (codecChannels)
    float inputGain = 1.0f; // fixed digital gain multiplier, input direction only (see audio_codec_get_input_gain_multiplier)
    std::vector<uint8_t> codecBuffer;    // raw codec-rate/codec-channel PCM, scratch
    std::vector<uint8_t> convertBuffer;  // intermediate scratch for the second conversion stage

    // Lifetime guard: closeStream() can be triggered from a different task than the one
    // doing read()/write() (e.g. the Settings UI disabling output while SfxEngine's audio
    // task is mid-write). `closing` keeps new I/O calls out, `busyCount` tracks I/O calls
    // currently in flight, and `drainSemaphore` lets closeStream() block until they finish
    // before freeing the handle. All three are only touched while `AudioStreamData::mutex`
    // is held, except for the give/take on drainSemaphore itself.
    bool closing = false;
    int busyCount = 0;
    SemaphoreHandle_t drainSemaphore = nullptr;
};

struct AudioStreamData {
    Device* inputCodec = nullptr;
    Device* outputCodec = nullptr;
    bool inputEnabled = true;
    bool outputEnabled = true;

    // Codecs reject volume/mute/gain calls until their chip is initialized, which only
    // happens when a stream of that direction is first opened (audio_codec_open ->
    // esp_codec_dev_open -> chip's open()/enable()). A Settings UI must be able to set
    // these regardless of whether anything is currently streaming, so we cache the desired
    // values here, apply them best-effort immediately, and replay them once the codec opens.
    float inputVolume = 100.0f;
    float outputVolume = 100.0f;
    bool inputMuted = false;
    bool outputMuted = false;
    AudioStreamHandleImpl* openInput = nullptr;
    AudioStreamHandleImpl* openOutput = nullptr;
    // Guards openInput/openOutput and the closing/busyCount fields of any handle reachable
    // through them, so close (possibly forced by setEnabled) can't race with read/write.
    SemaphoreHandle_t mutex = nullptr;

    AudioStreamChangeCallback changeCallback = nullptr;
    void* changeCallbackUserData = nullptr;
};

// Reads the registered change callback under the lock, then invokes it outside the lock
// (same lock-then-release-then-act pattern as codecForDirection).
void notifyChange(AudioStreamData* data, Device* device, enum AudioCodecDirection direction, enum AudioStreamChange change) {
    xSemaphoreTake(data->mutex, portMAX_DELAY);
    AudioStreamChangeCallback callback = data->changeCallback;
    void* userData = data->changeCallbackUserData;
    xSemaphoreGive(data->mutex);

    if (callback != nullptr) {
        callback(device, direction, change, userData);
    }
}

#define GET_DATA(device) (static_cast<AudioStreamData*>(device_get_driver_data(device)))

struct CodecSearchContext {
    enum AudioCodecDirection wantedDirection;
    Device* exactMatch = nullptr;
    Device* fallbackMatch = nullptr;
};

bool findCodecByDirection(Device* device, void* contextPtr) {
    auto* context = static_cast<CodecSearchContext*>(contextPtr);
    if (!device_is_ready(device)) {
        return true; // continue searching
    }

    enum AudioCodecDirection capabilities = AUDIO_CODEC_DIR_BOTH;
    if (audio_codec_get_capabilities(device, &capabilities) != ERROR_NONE) {
        return true;
    }

    if (capabilities == context->wantedDirection) {
        // Dedicated codec for exactly this direction (e.g. an input-only mic ADC) --
        // prefer it over a wider-capability codec and stop looking.
        context->exactMatch = device;
        return false;
    }

    if ((capabilities & context->wantedDirection) == context->wantedDirection
        && context->fallbackMatch == nullptr) {
        // Supports the wanted direction as part of a wider capability set (e.g. a
        // BOTH-capable codec asked for INPUT). Remember it but keep looking -- boards
        // commonly pair a BOTH-capable output codec with a separate dedicated input
        // ADC, and binding the wrong one makes both directions fight over the same
        // physical esp_codec_dev handle (reopen for input reconfigures/breaks output).
        context->fallbackMatch = device;
    }

    return true;
}

Device* findFirstCodecSupporting(enum AudioCodecDirection direction) {
    CodecSearchContext context = { .wantedDirection = direction };
    device_for_each_of_type(&AUDIO_CODEC_TYPE, &context, findCodecByDirection);
    return (context.exactMatch != nullptr) ? context.exactMatch : context.fallbackMatch;
}

// The audio-stream device is constructed while modules start, which happens before the
// device tree's codec devices (nested under i2c0) are started. So codecs can't be resolved
// at start_device time — resolve (and cache) them lazily on first use instead, by which
// point the device tree has finished starting.
Device* codecForDirection(AudioStreamData* data, enum AudioCodecDirection direction) {
    Device** slot = (direction == AUDIO_CODEC_DIR_INPUT) ? &data->inputCodec : &data->outputCodec;

    // Fast path: already resolved (the common case after first use).
    Device* existing = *slot;
    if (existing != nullptr) {
        return existing;
    }

    // Slow path: resolve outside the lock (device_for_each_of_type can be costly), then
    // re-check under the mutex before committing -- avoids two threads racing to write
    // *slot (and double-logging "Bound ... codec").
    Device* found = findFirstCodecSupporting(direction);

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

// Marks an I/O operation as in-flight on `handle`, preventing closeStream() from freeing it
// underneath us. Returns false (and does nothing further) if the handle is closing/closed --
// callers must bail out with an error in that case. Must be paired with ioEnd().
bool ioBegin(AudioStreamData* data, AudioStreamHandleImpl* handle) {
    xSemaphoreTake(data->mutex, portMAX_DELAY);
    bool isInput = (handle->direction == AUDIO_CODEC_DIR_INPUT);
    AudioStreamHandleImpl* slotValue = isInput ? data->openInput : data->openOutput;
    if (slotValue != handle || handle->closing) {
        xSemaphoreGive(data->mutex);
        return false;
    }
    handle->busyCount++;
    xSemaphoreGive(data->mutex);
    return true;
}

void ioEnd(AudioStreamData* data, AudioStreamHandleImpl* handle) {
    xSemaphoreTake(data->mutex, portMAX_DELAY);
    handle->busyCount--;
    if (handle->closing && handle->busyCount == 0) {
        xSemaphoreGive(handle->drainSemaphore);
    }
    xSemaphoreGive(data->mutex);
}

// region AudioStreamApi

error_t openStream(Device* device, const struct AudioStreamConfig* config, enum AudioCodecDirection direction, AudioStreamHandle* outHandle) {
    if (config == nullptr || outHandle == nullptr) {
        return ERROR_INVALID_ARGUMENT;
    }

    if (config->bits_per_sample != 8 && config->bits_per_sample != 16
        && config->bits_per_sample != 24 && config->bits_per_sample != 32) {
        // bytesPerFrame/codecBytesPerFrame below assume a whole number of bytes per sample;
        // anything else corrupts every frame-size calculation in read/writeStream.
        return ERROR_INVALID_ARGUMENT;
    }

    if (config->channels == 0) {
        return ERROR_INVALID_ARGUMENT;
    }

    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_RESOURCE;
    }

    bool isInput = (direction == AUDIO_CODEC_DIR_INPUT);
    Device* codec = codecForDirection(data, direction);
    if (codec == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }

    xSemaphoreTake(data->mutex, portMAX_DELAY);

    if ((isInput && !data->inputEnabled) || (!isInput && !data->outputEnabled)) {
        xSemaphoreGive(data->mutex);
        return ERROR_NOT_ALLOWED;
    }

    AudioStreamHandleImpl** slot = isInput ? &data->openInput : &data->openOutput;
    if (*slot != nullptr) {
        xSemaphoreGive(data->mutex);
        return ERROR_INVALID_STATE;
    }

    // Reserve the slot with a placeholder so concurrent opens can't race past the check
    // above while we do the (potentially slow) codec open below outside the lock.
    auto* reservation = reinterpret_cast<AudioStreamHandleImpl*>(1);
    *slot = reservation;
    xSemaphoreGive(data->mutex);

    uint32_t codecRate = 0;
    if (audio_codec_get_native_sample_rate(codec, direction, &codecRate) != ERROR_NONE || codecRate == 0) {
        xSemaphoreTake(data->mutex, portMAX_DELAY);
        if (*slot == reservation) { *slot = nullptr; }
        xSemaphoreGive(data->mutex);
        return ERROR_RESOURCE;
    }

    // The codec must be opened with its native channel layout (e.g. 4 for a 4-slot TDM mic
    // ADC) -- opening it with the app's requested channel count can silently corrupt the
    // stream (ES7210 in TDM mode halves its configured bit depth for <= 2 channels). We
    // convert between the codec's layout and the app's requested channel count ourselves.
    uint8_t codecChannels = config->channels;
    if (audio_codec_get_native_channels(codec, direction, &codecChannels) != ERROR_NONE || codecChannels == 0) {
        codecChannels = config->channels;
    }

    struct AudioCodecStreamConfig codecConfig = {
        .sample_rate = codecRate,
        .bits_per_sample = config->bits_per_sample,
        .channels = codecChannels,
        .direction = direction,
    };

    if (audio_codec_open(codec, &codecConfig) != ERROR_NONE) {
        LOG_E(TAG, "Failed to open codec for %s", isInput ? "input" : "output");
        xSemaphoreTake(data->mutex, portMAX_DELAY);
        if (*slot == reservation) { *slot = nullptr; }
        xSemaphoreGive(data->mutex);
        return ERROR_RESOURCE;
    }

    // The chip is initialized now -- replay any volume/mute settings that were requested
    // before this stream existed (set_volume/set_mute cache them but the chip rejects them
    // until opened).
    if (isInput) {
        audio_codec_set_volume(codec, AUDIO_CODEC_DIR_INPUT, data->inputVolume);
        audio_codec_set_mute(codec, AUDIO_CODEC_DIR_INPUT, data->inputMuted);
    } else {
        audio_codec_set_volume(codec, AUDIO_CODEC_DIR_OUTPUT, data->outputVolume);
        audio_codec_set_mute(codec, AUDIO_CODEC_DIR_OUTPUT, data->outputMuted);
    }

    auto* handle = new AudioStreamHandleImpl();
    handle->device = device;
    handle->direction = direction;
    handle->config = *config;
    handle->codecRate = codecRate;
    handle->codecChannels = codecChannels;
    handle->bytesPerFrame = (uint8_t) ((config->bits_per_sample / 8) * config->channels);
    handle->codecBytesPerFrame = (uint8_t) ((config->bits_per_sample / 8) * codecChannels);
    if (isInput) {
        audio_codec_get_input_gain_multiplier(codec, &handle->inputGain);
    }
    handle->drainSemaphore = xSemaphoreCreateBinary();
    if (handle->drainSemaphore == nullptr) {
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

    *outHandle = handle;
    return ERROR_NONE;
}

error_t openInput(Device* device, const struct AudioStreamConfig* config, AudioStreamHandle* outHandle) {
    return openStream(device, config, AUDIO_CODEC_DIR_INPUT, outHandle);
}

error_t openOutput(Device* device, const struct AudioStreamConfig* config, AudioStreamHandle* outHandle) {
    return openStream(device, config, AUDIO_CODEC_DIR_OUTPUT, outHandle);
}

error_t readStream(AudioStreamHandle handleBase, void* outData, size_t dataSize, size_t* bytesRead, TickType_t timeout) {
    auto* handle = static_cast<AudioStreamHandleImpl*>(handleBase);
    if (handle->direction != AUDIO_CODEC_DIR_INPUT || handle->bytesPerFrame == 0) {
        return ERROR_INVALID_STATE;
    }

    auto* data = GET_DATA(handle->device);
    if (data == nullptr || data->inputCodec == nullptr) {
        return ERROR_RESOURCE;
    }

    size_t requestedFrames = dataSize / handle->bytesPerFrame;
    if (requestedFrames == 0) {
        if (bytesRead != nullptr) {
            *bytesRead = 0;
        }
        return ERROR_NONE;
    }

    if (!ioBegin(data, handle)) {
        return ERROR_INVALID_STATE;
    }

    bool sameRate = (handle->codecRate == handle->config.sample_rate);
    bool sameChannels = (handle->codecChannels == handle->config.channels);

    error_t result;
    if (sameRate && sameChannels) {
        size_t codecBytesRead = 0;
        result = audio_codec_read(data->inputCodec, outData, dataSize, &codecBytesRead, timeout);
        if (bytesRead != nullptr) {
            *bytesRead = codecBytesRead;
        }
    } else {
        // Read enough codec-rate frames to produce the requested number of output-rate frames.
        size_t codecFrames = (size_t) ((double) requestedFrames * ((double) handle->codecRate / (double) handle->config.sample_rate)) + 2;
        size_t codecBytesNeeded = codecFrames * handle->codecBytesPerFrame;
        if (handle->codecBuffer.size() < codecBytesNeeded) {
            handle->codecBuffer.resize(codecBytesNeeded);
        }

        size_t codecBytesRead = 0;
        result = audio_codec_read(data->inputCodec, handle->codecBuffer.data(), codecBytesNeeded, &codecBytesRead, timeout);
        if (result == ERROR_NONE) {
            size_t codecFramesRead = codecBytesRead / handle->codecBytesPerFrame;
            const int16_t* rateInput = reinterpret_cast<const int16_t*>(handle->codecBuffer.data());
            uint8_t rateInputChannels = handle->codecChannels;
            size_t rateInputFrames = codecFramesRead;

            // Downmix first (while still at the codec's higher rate -- cheaper) if needed.
            if (!sameChannels) {
                size_t convertBytesNeeded = codecFramesRead * handle->bytesPerFrame;
                if (handle->convertBuffer.size() < convertBytesNeeded) {
                    handle->convertBuffer.resize(convertBytesNeeded);
                }
                convertChannelsS16(rateInput, codecFramesRead, handle->codecChannels,
                                   reinterpret_cast<int16_t*>(handle->convertBuffer.data()), handle->config.channels);
                rateInput = reinterpret_cast<const int16_t*>(handle->convertBuffer.data());
                rateInputChannels = handle->config.channels;
            }

            size_t outFrames = resampleS16(
                rateInput, rateInputFrames, rateInputChannels,
                handle->codecRate, handle->config.sample_rate,
                reinterpret_cast<int16_t*>(outData), requestedFrames);

            if (bytesRead != nullptr) {
                *bytesRead = outFrames * handle->bytesPerFrame;
            }
        }
    }

    if (result == ERROR_NONE && handle->inputGain != 1.0f && bytesRead != nullptr && *bytesRead > 0) {
        auto* samples = reinterpret_cast<int16_t*>(outData);
        size_t sampleCount = *bytesRead / sizeof(int16_t);
        for (size_t i = 0; i < sampleCount; i++) {
            float boosted = (float) samples[i] * handle->inputGain;
            samples[i] = (int16_t) (boosted < -32768.0f ? -32768.0f : boosted > 32767.0f ? 32767.0f : boosted);
        }
    }

    ioEnd(data, handle);
    return result;
}

error_t writeStream(AudioStreamHandle handleBase, const void* inData, size_t dataSize, size_t* bytesWritten, TickType_t timeout) {
    auto* handle = static_cast<AudioStreamHandleImpl*>(handleBase);
    if (handle->direction != AUDIO_CODEC_DIR_OUTPUT || handle->bytesPerFrame == 0) {
        return ERROR_INVALID_STATE;
    }

    auto* data = GET_DATA(handle->device);
    if (data == nullptr || data->outputCodec == nullptr) {
        return ERROR_RESOURCE;
    }

    size_t inFrames = dataSize / handle->bytesPerFrame;
    if (inFrames == 0) {
        if (bytesWritten != nullptr) {
            *bytesWritten = 0;
        }
        return ERROR_NONE;
    }

    if (!ioBegin(data, handle)) {
        return ERROR_INVALID_STATE;
    }

    bool sameRate = (handle->codecRate == handle->config.sample_rate);
    bool sameChannels = (handle->codecChannels == handle->config.channels);

    error_t result;
    if (sameRate && sameChannels) {
        size_t codecBytesWritten = 0;
        result = audio_codec_write(data->outputCodec, inData, dataSize, &codecBytesWritten, timeout);
        if (bytesWritten != nullptr) {
            // Report in terms of input bytes consumed, matching codecBytesWritten 1:1 here.
            *bytesWritten = codecBytesWritten;
        }
    } else {
        const int16_t* rateInput = reinterpret_cast<const int16_t*>(inData);
        uint8_t rateInputChannels = handle->config.channels;
        size_t rateInputFrames = inFrames;

        // Upmix/downmix first (while still at the app's rate -- cheaper if downmixing) if needed.
        if (!sameChannels) {
            size_t convertBytesNeeded = inFrames * handle->codecBytesPerFrame;
            if (handle->convertBuffer.size() < convertBytesNeeded) {
                handle->convertBuffer.resize(convertBytesNeeded);
            }
            convertChannelsS16(rateInput, inFrames, handle->config.channels,
                               reinterpret_cast<int16_t*>(handle->convertBuffer.data()), handle->codecChannels);
            rateInput = reinterpret_cast<const int16_t*>(handle->convertBuffer.data());
            rateInputChannels = handle->codecChannels;
        }

        size_t codecFrameCapacity = (size_t) ((double) rateInputFrames * ((double) handle->codecRate / (double) handle->config.sample_rate)) + 2;
        size_t codecBytesCapacity = codecFrameCapacity * handle->codecBytesPerFrame;
        if (handle->codecBuffer.size() < codecBytesCapacity) {
            handle->codecBuffer.resize(codecBytesCapacity);
        }

        size_t codecFrames;
        if (sameRate) {
            codecFrames = rateInputFrames;
            std::memcpy(handle->codecBuffer.data(), rateInput, rateInputFrames * handle->codecBytesPerFrame);
        } else {
            codecFrames = resampleS16(
                rateInput, rateInputFrames, rateInputChannels,
                handle->config.sample_rate, handle->codecRate,
                reinterpret_cast<int16_t*>(handle->codecBuffer.data()), codecFrameCapacity);
        }

        size_t codecBytesToWrite = codecFrames * handle->codecBytesPerFrame;
        size_t codecBytesWritten = 0;
        result = audio_codec_write(data->outputCodec, handle->codecBuffer.data(), codecBytesToWrite, &codecBytesWritten, timeout);
        if (result == ERROR_NONE && bytesWritten != nullptr) {
            // The caller provided `dataSize` worth of input; we consumed all of it (resampled/converted).
            *bytesWritten = dataSize;
        }
    }

    ioEnd(data, handle);
    return result;
}

error_t closeStream(AudioStreamHandle handleBase) {
    auto* handle = static_cast<AudioStreamHandleImpl*>(handleBase);
    auto* data = GET_DATA(handle->device);
    if (data == nullptr) {
        return ERROR_RESOURCE;
    }

    bool isInput = (handle->direction == AUDIO_CODEC_DIR_INPUT);
    Device* codec = isInput ? data->inputCodec : data->outputCodec;
    AudioStreamHandleImpl** slot = isInput ? &data->openInput : &data->openOutput;

    xSemaphoreTake(data->mutex, portMAX_DELAY);
    if (handle->closing) {
        // Already being closed by another caller (e.g. concurrent setEnabled + app close).
        xSemaphoreGive(data->mutex);
        return ERROR_NONE;
    }
    handle->closing = true;
    if (*slot == handle) {
        *slot = nullptr;
    }
    bool mustDrain = (handle->busyCount > 0);
    xSemaphoreGive(data->mutex);

    if (mustDrain && handle->drainSemaphore != nullptr) {
        xSemaphoreTake(handle->drainSemaphore, portMAX_DELAY);
    }

    if (codec != nullptr) {
        audio_codec_close(codec);
    }

    if (handle->drainSemaphore != nullptr) {
        vSemaphoreDelete(handle->drainSemaphore);
    }

    delete handle;
    return ERROR_NONE;
}

error_t setVolume(Device* device, enum AudioCodecDirection direction, float volumePercent) {
    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_RESOURCE;
    }
    Device* codec = codecForDirection(data, direction);
    if (codec == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }

    bool isInput = (direction == AUDIO_CODEC_DIR_INPUT);
    xSemaphoreTake(data->mutex, portMAX_DELAY);
    if (isInput) { data->inputVolume = volumePercent; } else { data->outputVolume = volumePercent; }
    xSemaphoreGive(data->mutex);

    // The chip rejects this until it's been opened by a stream of this direction; that's
    // fine -- the cached value above gets replayed in openStream() once it is.
    audio_codec_set_volume(codec, direction, volumePercent);
    notifyChange(data, device, direction, AUDIO_STREAM_CHANGE_VOLUME);
    return ERROR_NONE;
}

error_t getVolume(Device* device, enum AudioCodecDirection direction, float* volumePercent) {
    auto* data = GET_DATA(device);
    if (data == nullptr || volumePercent == nullptr) {
        return ERROR_RESOURCE;
    }
    Device* codec = codecForDirection(data, direction);
    if (codec == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }

    if (audio_codec_get_volume(codec, direction, volumePercent) == ERROR_NONE) {
        return ERROR_NONE;
    }

    // Codec not open yet -- report the cached/desired value instead.
    xSemaphoreTake(data->mutex, portMAX_DELAY);
    *volumePercent = (direction == AUDIO_CODEC_DIR_INPUT) ? data->inputVolume : data->outputVolume;
    xSemaphoreGive(data->mutex);
    return ERROR_NONE;
}

error_t setMute(Device* device, enum AudioCodecDirection direction, bool muted) {
    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_RESOURCE;
    }
    Device* codec = codecForDirection(data, direction);
    if (codec == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }

    bool isInput = (direction == AUDIO_CODEC_DIR_INPUT);
    xSemaphoreTake(data->mutex, portMAX_DELAY);
    if (isInput) { data->inputMuted = muted; } else { data->outputMuted = muted; }
    xSemaphoreGive(data->mutex);

    // As with volume, the chip may reject this until opened; cached value is replayed
    // in openStream().
    audio_codec_set_mute(codec, direction, muted);
    notifyChange(data, device, direction, AUDIO_STREAM_CHANGE_MUTE);
    return ERROR_NONE;
}

error_t getMute(Device* device, enum AudioCodecDirection direction, bool* muted) {
    auto* data = GET_DATA(device);
    if (data == nullptr || muted == nullptr) {
        return ERROR_RESOURCE;
    }
    Device* codec = codecForDirection(data, direction);
    if (codec == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }

    if (audio_codec_get_mute(codec, direction, muted) == ERROR_NONE) {
        return ERROR_NONE;
    }

    xSemaphoreTake(data->mutex, portMAX_DELAY);
    *muted = (direction == AUDIO_CODEC_DIR_INPUT) ? data->inputMuted : data->outputMuted;
    xSemaphoreGive(data->mutex);
    return ERROR_NONE;
}

error_t setEnabled(Device* device, enum AudioCodecDirection direction, bool enabled) {
    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_RESOURCE;
    }

    bool isInput = (direction == AUDIO_CODEC_DIR_INPUT);
    Device* codec = codecForDirection(data, direction);
    if (codec == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }

    xSemaphoreTake(data->mutex, portMAX_DELAY);
    if (isInput) {
        data->inputEnabled = enabled;
    } else {
        data->outputEnabled = enabled;
    }

    // Capture and clear the slot under the lock so we hand closeStream() a pointer that
    // can't simultaneously be torn down by a racing close from the owning app (closeStream
    // re-checks `*slot == handle` and no-ops if it's already been cleared/replaced).
    AudioStreamHandleImpl* toClose = nullptr;
    if (!enabled) {
        AudioStreamHandleImpl** slot = isInput ? &data->openInput : &data->openOutput;
        toClose = *slot;
    }
    xSemaphoreGive(data->mutex);

    if (toClose != nullptr) {
        closeStream(toClose);
    }

    notifyChange(data, device, direction, AUDIO_STREAM_CHANGE_ENABLED);
    return ERROR_NONE;
}

error_t getEnabled(Device* device, enum AudioCodecDirection direction, bool* enabled) {
    auto* data = GET_DATA(device);
    if (data == nullptr || enabled == nullptr) {
        return ERROR_RESOURCE;
    }
    Device* codec = codecForDirection(data, direction);
    if (codec == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    xSemaphoreTake(data->mutex, portMAX_DELAY);
    *enabled = (direction == AUDIO_CODEC_DIR_INPUT) ? data->inputEnabled : data->outputEnabled;
    xSemaphoreGive(data->mutex);
    return ERROR_NONE;
}

error_t isSupported(Device* device, enum AudioCodecDirection direction, bool* supported) {
    auto* data = GET_DATA(device);
    if (data == nullptr || supported == nullptr) {
        return ERROR_RESOURCE;
    }
    *supported = codecForDirection(data, direction) != nullptr;
    return ERROR_NONE;
}

error_t setChangeCallback(Device* device, AudioStreamChangeCallback callback, void* userData) {
    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_RESOURCE;
    }

    xSemaphoreTake(data->mutex, portMAX_DELAY);
    data->changeCallback = callback;
    data->changeCallbackUserData = userData;
    xSemaphoreGive(data->mutex);
    return ERROR_NONE;
}

const struct AudioStreamApi API = {
    .open_input = openInput,
    .open_output = openOutput,
    .read = readStream,
    .write = writeStream,
    .close = closeStream,
    .set_volume = setVolume,
    .get_volume = getVolume,
    .set_mute = setMute,
    .get_mute = getMute,
    .set_enabled = setEnabled,
    .get_enabled = getEnabled,
    .is_supported = isSupported,
    .set_change_callback = setChangeCallback,
};

// endregion

// region Driver lifecycle

error_t startDevice(Device* device) {
    auto* data = new AudioStreamData();
    data->mutex = xSemaphoreCreateMutex();
    if (data->mutex == nullptr) {
        delete data;
        return ERROR_OUT_OF_MEMORY;
    }
    device_set_driver_data(device, data);
    return ERROR_NONE;
}

error_t stopDevice(Device* device) {
    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_NONE;
    }

    if (data->openInput != nullptr) {
        closeStream(data->openInput);
    }
    if (data->openOutput != nullptr) {
        closeStream(data->openOutput);
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
    .start_device = startDevice,
    .stop_device = stopDevice,
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
