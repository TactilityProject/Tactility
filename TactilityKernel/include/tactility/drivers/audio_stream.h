// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include <tactility/freertos/freertos.h>
#include <tactility/error.h>
#include <tactility/drivers/audio_codec.h>

struct Device;

/**
 * @brief Handle to an open audio stream (see audio_stream_open_input/open_output).
 *
 * The `device` field identifies the owning audio stream device so that
 * audio_stream_read/write/close can dispatch to the correct driver; implementations
 * embed this as the first member of a larger private struct and may store additional
 * state after it.
 */
struct AudioStreamHandleData {
    struct Device* device;
};

typedef struct AudioStreamHandleData* AudioStreamHandle;

/**
 * @brief Stream configuration requested by the caller.
 *
 * The sample_rate is the rate the caller wants to read/write at; the audio-stream
 * device transparently resamples to/from the bound codec's native rate, so the same
 * app code works regardless of which codec hardware is present.
 */
struct AudioStreamConfig {
    uint32_t sample_rate;     // e.g. 16000, 44100, 48000
    uint8_t bits_per_sample;  // 16, 24, 32
    uint8_t channels;
};

/** @brief Identifies which cached field changed in an AudioStreamChangeCallback. */
enum AudioStreamChange {
    AUDIO_STREAM_CHANGE_VOLUME,
    AUDIO_STREAM_CHANGE_MUTE,
    AUDIO_STREAM_CHANGE_ENABLED,
};

typedef void (*AudioStreamChangeCallback)(struct Device* device, enum AudioCodecDirection direction, enum AudioStreamChange change, void* user_data);

/**
 * @brief API for the high-level full-duplex audio stream device.
 *
 * Implementations bind to one input-capable and/or one output-capable AUDIO_CODEC_TYPE
 * device, and provide resampling plus shared volume/mute/enable state on top. This is
 * the API apps (including ELF side-loaded apps) and AudioService should use — most code
 * should not talk to AUDIO_CODEC_TYPE devices directly.
 *
 * read/write are blocking and must be called from the caller's own task, never from the
 * main/LVGL thread.
 */
struct AudioStreamApi {
    /**
     * @brief Opens an input (recording) stream at the requested configuration.
     * @param[in] device the audio stream device
     * @param[in] config the requested stream configuration
     * @param[out] out_handle receives the opened stream handle
     * @retval ERROR_NONE on success
     * @retval ERROR_NOT_SUPPORTED if no input-capable codec is bound
     * @retval ERROR_INVALID_STATE if an input stream is already open
     */
    error_t (*open_input)(struct Device* device, const struct AudioStreamConfig* config, AudioStreamHandle* out_handle);

    /**
     * @brief Opens an output (playback) stream at the requested configuration.
     * @param[in] device the audio stream device
     * @param[in] config the requested stream configuration
     * @param[out] out_handle receives the opened stream handle
     * @retval ERROR_NONE on success
     * @retval ERROR_NOT_SUPPORTED if no output-capable codec is bound
     * @retval ERROR_INVALID_STATE if an output stream is already open
     */
    error_t (*open_output)(struct Device* device, const struct AudioStreamConfig* config, AudioStreamHandle* out_handle);

    /**
     * @brief Reads recorded audio data from an input stream, resampled to the requested rate.
     * @param[in] handle the stream handle returned by open_input
     * @param[out] data the buffer to store the read data
     * @param[in] data_size the number of bytes to read
     * @param[out] bytes_read the number of bytes actually read
     * @param[in] timeout the maximum time to wait for the operation to complete
     * @retval ERROR_NONE on success
     * @retval ERROR_TIMEOUT when the operation timed out
     */
    error_t (*read)(AudioStreamHandle handle, void* data, size_t data_size, size_t* bytes_read, TickType_t timeout);

    /**
     * @brief Writes audio data for playback to an output stream, resampled from the requested rate.
     * @param[in] handle the stream handle returned by open_output
     * @param[in] data the buffer containing the data to write
     * @param[in] data_size the number of bytes to write
     * @param[out] bytes_written the number of bytes actually written
     * @param[in] timeout the maximum time to wait for the operation to complete
     * @retval ERROR_NONE on success
     * @retval ERROR_TIMEOUT when the operation timed out
     */
    error_t (*write)(AudioStreamHandle handle, const void* data, size_t data_size, size_t* bytes_written, TickType_t timeout);

    /**
     * @brief Closes a stream opened with open_input or open_output.
     * @param[in] handle the stream handle to close
     * @retval ERROR_NONE on success
     */
    error_t (*close)(AudioStreamHandle handle);

    /**
     * @brief Sets the shared volume for the given direction. Visible to all consumers
     * (other apps, AudioService, Settings UI) since the state lives on the bound codec.
     * @param[in] device the audio stream device
     * @param[in] direction AUDIO_CODEC_DIR_INPUT or AUDIO_CODEC_DIR_OUTPUT
     * @param[in] volume_percent volume in the range 0..100
     * @retval ERROR_NONE on success
     * @retval ERROR_NOT_SUPPORTED if no codec is bound for the given direction
     */
    error_t (*set_volume)(struct Device* device, enum AudioCodecDirection direction, float volume_percent);

    /** @brief Gets the shared volume for the given direction. See set_volume. */
    error_t (*get_volume)(struct Device* device, enum AudioCodecDirection direction, float* volume_percent);

    /** @brief Sets the shared mute state for the given direction. See set_volume. */
    error_t (*set_mute)(struct Device* device, enum AudioCodecDirection direction, bool muted);

    /** @brief Gets the shared mute state for the given direction. See set_volume. */
    error_t (*get_mute)(struct Device* device, enum AudioCodecDirection direction, bool* muted);

    /**
     * @brief Enables or disables the given direction. A disabled direction cannot be
     * opened (open_input/open_output return ERROR_NOT_ALLOWED) and any open stream in
     * that direction is closed. Used by Settings UI / AudioService to govern access.
     * @param[in] device the audio stream device
     * @param[in] direction AUDIO_CODEC_DIR_INPUT or AUDIO_CODEC_DIR_OUTPUT
     * @param[in] enabled true to enable, false to disable
     * @retval ERROR_NONE on success
     * @retval ERROR_NOT_SUPPORTED if no codec is bound for the given direction
     */
    error_t (*set_enabled)(struct Device* device, enum AudioCodecDirection direction, bool enabled);

    /** @brief Gets the enabled state for the given direction. See set_enabled. */
    error_t (*get_enabled)(struct Device* device, enum AudioCodecDirection direction, bool* enabled);

    /**
     * @brief Checks whether a codec supporting the given direction is bound.
     * @param[in] device the audio stream device
     * @param[in] direction AUDIO_CODEC_DIR_INPUT or AUDIO_CODEC_DIR_OUTPUT
     * @param[out] supported true if a codec for this direction is bound, false otherwise
     * @retval ERROR_NONE on success
     */
    error_t (*is_supported)(struct Device* device, enum AudioCodecDirection direction, bool* supported);

    /** @brief See audio_stream_set_change_callback. */
    error_t (*set_change_callback)(struct Device* device, AudioStreamChangeCallback callback, void* user_data);
};

/** @brief See AudioStreamApi::open_input */
error_t audio_stream_open_input(struct Device* device, const struct AudioStreamConfig* config, AudioStreamHandle* out_handle);

/** @brief See AudioStreamApi::open_output */
error_t audio_stream_open_output(struct Device* device, const struct AudioStreamConfig* config, AudioStreamHandle* out_handle);

/** @brief See AudioStreamApi::read */
error_t audio_stream_read(AudioStreamHandle handle, void* data, size_t data_size, size_t* bytes_read, TickType_t timeout);

/** @brief See AudioStreamApi::write */
error_t audio_stream_write(AudioStreamHandle handle, const void* data, size_t data_size, size_t* bytes_written, TickType_t timeout);

/** @brief See AudioStreamApi::close */
error_t audio_stream_close(AudioStreamHandle handle);

/** @brief See AudioStreamApi::set_volume */
error_t audio_stream_set_volume(struct Device* device, enum AudioCodecDirection direction, float volume_percent);

/** @brief See AudioStreamApi::get_volume */
error_t audio_stream_get_volume(struct Device* device, enum AudioCodecDirection direction, float* volume_percent);

/** @brief See AudioStreamApi::set_mute */
error_t audio_stream_set_mute(struct Device* device, enum AudioCodecDirection direction, bool muted);

/** @brief See AudioStreamApi::get_mute */
error_t audio_stream_get_mute(struct Device* device, enum AudioCodecDirection direction, bool* muted);

/** @brief See AudioStreamApi::set_enabled */
error_t audio_stream_set_enabled(struct Device* device, enum AudioCodecDirection direction, bool enabled);

/** @brief See AudioStreamApi::get_enabled */
error_t audio_stream_get_enabled(struct Device* device, enum AudioCodecDirection direction, bool* enabled);

/** @brief See AudioStreamApi::is_supported */
error_t audio_stream_is_supported(struct Device* device, enum AudioCodecDirection direction, bool* supported);

/** @brief See AudioStreamApi::set_change_callback */
error_t audio_stream_set_change_callback(struct Device* device, AudioStreamChangeCallback callback, void* user_data);

extern const struct DeviceType AUDIO_STREAM_TYPE;

#ifdef __cplusplus
}
#endif
