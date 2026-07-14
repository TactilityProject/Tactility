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

struct Device;

/**
 * @brief Direction of an audio codec operation or capability.
 */
enum AudioCodecDirection {
    AUDIO_CODEC_DIR_INPUT = 1,
    AUDIO_CODEC_DIR_OUTPUT = 2,
    AUDIO_CODEC_DIR_BOTH = 3,
};

/**
 * @brief Stream configuration used to open a codec for reading or writing.
 */
struct AudioCodecStreamConfig {
    uint32_t sample_rate;
    uint8_t bits_per_sample; // 16, 24, 32
    uint8_t channels;
    enum AudioCodecDirection direction;
};

/**
 * @brief API for audio codec drivers (e.g. ES8388, ES7210, AW88298).
 *
 * Implementations typically wrap a vendor codec library (e.g. esp_codec_dev) and
 * the underlying I2S/I2C/GPIO controllers. This API is intentionally low-level and
 * codec-agnostic: it does not perform resampling or mixing. Most apps should use
 * the higher-level audio_stream API (see audio_stream.h) instead.
 */
struct AudioCodecApi {
    /**
     * @brief Opens the codec for streaming in the given direction(s).
     * @param[in] device the audio codec device
     * @param[in] config the stream configuration
     * @retval ERROR_NONE on success
     * @retval ERROR_NOT_SUPPORTED if the requested direction is not supported
     */
    error_t (*open)(struct Device* device, const struct AudioCodecStreamConfig* config);

    /**
     * @brief Closes the codec, stopping any active streams.
     * @param[in] device the audio codec device
     * @retval ERROR_NONE on success
     */
    error_t (*close)(struct Device* device);

    /**
     * @brief Reads recorded audio data from the codec. Only valid for codecs opened with input direction.
     * @param[in] device the audio codec device
     * @param[out] data the buffer to store the read data
     * @param[in] data_size the number of bytes to read
     * @param[out] bytes_read the number of bytes actually read
     * @param[in] timeout the maximum time to wait for the operation to complete
     * @retval ERROR_NONE on success
     * @retval ERROR_TIMEOUT when the operation timed out
     */
    error_t (*read)(struct Device* device, void* data, size_t data_size, size_t* bytes_read, TickType_t timeout);

    /**
     * @brief Writes audio data for playback to the codec. Only valid for codecs opened with output direction.
     * @param[in] device the audio codec device
     * @param[in] data the buffer containing the data to write
     * @param[in] data_size the number of bytes to write
     * @param[out] bytes_written the number of bytes actually written
     * @param[in] timeout the maximum time to wait for the operation to complete
     * @retval ERROR_NONE on success
     * @retval ERROR_TIMEOUT when the operation timed out
     */
    error_t (*write)(struct Device* device, const void* data, size_t data_size, size_t* bytes_written, TickType_t timeout);

    /**
     * @brief Sets the volume for the given direction.
     * @param[in] device the audio codec device
     * @param[in] direction AUDIO_CODEC_DIR_INPUT or AUDIO_CODEC_DIR_OUTPUT
     * @param[in] volume_percent volume in the range 0..100
     * @retval ERROR_NONE on success
     * @retval ERROR_NOT_SUPPORTED if the direction is not supported by this codec
     */
    error_t (*set_volume)(struct Device* device, enum AudioCodecDirection direction, float volume_percent);

    /**
     * @brief Gets the volume for the given direction.
     * @param[in] device the audio codec device
     * @param[in] direction AUDIO_CODEC_DIR_INPUT or AUDIO_CODEC_DIR_OUTPUT
     * @param[out] volume_percent volume in the range 0..100
     * @retval ERROR_NONE on success
     * @retval ERROR_NOT_SUPPORTED if the direction is not supported by this codec
     */
    error_t (*get_volume)(struct Device* device, enum AudioCodecDirection direction, float* volume_percent);

    /**
     * @brief Mutes or unmutes the given direction.
     * @param[in] device the audio codec device
     * @param[in] direction AUDIO_CODEC_DIR_INPUT or AUDIO_CODEC_DIR_OUTPUT
     * @param[in] muted true to mute, false to unmute
     * @retval ERROR_NONE on success
     * @retval ERROR_NOT_SUPPORTED if the direction is not supported by this codec
     */
    error_t (*set_mute)(struct Device* device, enum AudioCodecDirection direction, bool muted);

    /**
     * @brief Gets the mute state for the given direction.
     * @param[in] device the audio codec device
     * @param[in] direction AUDIO_CODEC_DIR_INPUT or AUDIO_CODEC_DIR_OUTPUT
     * @param[out] muted true if muted, false otherwise
     * @retval ERROR_NONE on success
     * @retval ERROR_NOT_SUPPORTED if the direction is not supported by this codec
     */
    error_t (*get_mute)(struct Device* device, enum AudioCodecDirection direction, bool* muted);

    /**
     * @brief Gets the native sample rate the codec hardware runs at for the given direction.
     * @param[in] device the audio codec device
     * @param[in] direction AUDIO_CODEC_DIR_INPUT or AUDIO_CODEC_DIR_OUTPUT
     * @param[out] rate_hz the native sample rate in Hz
     * @retval ERROR_NONE on success
     * @retval ERROR_NOT_SUPPORTED if the direction is not supported by this codec
     */
    error_t (*get_native_sample_rate)(struct Device* device, enum AudioCodecDirection direction, uint32_t* rate_hz);

    /**
     * @brief Gets the number of channels the codec hardware natively expects/produces for the
     * given direction (e.g. a 4-slot TDM microphone ADC reports 4 here even if an app only
     * wants 1). Callers that need a different channel count must downmix/upmix themselves --
     * opening the codec with a channel count that doesn't match its native layout can produce
     * corrupted audio (e.g. ES7210 in TDM mode silently halves its configured bit depth when
     * asked for <= 2 channels).
     * @param[in] device the audio codec device
     * @param[in] direction AUDIO_CODEC_DIR_INPUT or AUDIO_CODEC_DIR_OUTPUT
     * @param[out] channels the native channel count
     * @retval ERROR_NONE on success
     * @retval ERROR_NOT_SUPPORTED if the direction is not supported by this codec
     */
    error_t (*get_native_channels)(struct Device* device, enum AudioCodecDirection direction, uint8_t* channels);

    /**
     * @brief Gets the directions supported by this codec.
     * @param[in] device the audio codec device
     * @param[out] supported_directions AUDIO_CODEC_DIR_INPUT, AUDIO_CODEC_DIR_OUTPUT, or AUDIO_CODEC_DIR_BOTH
     * @retval ERROR_NONE on success
     */
    error_t (*get_capabilities)(struct Device* device, enum AudioCodecDirection* supported_directions);

    /**
     * @brief Gets a fixed digital gain multiplier to apply to this codec's input samples,
     * on top of whatever audio_codec_set_volume() controls. Compensates for mic capsules
     * that are quiet even at full hardware gain (e.g. small PDM/MEMS mics), or codecs with
     * no working hardware gain control at all (e.g. a dummy PDM codec). Set from the
     * codec's own devicetree config; 1.0 (no boost) if this field is NULL or the codec
     * doesn't implement it.
     * @param[in] device the audio codec device
     * @param[out] gain the multiplier to apply (1.0 = no change)
     * @retval ERROR_NONE on success
     * @retval ERROR_NOT_SUPPORTED if the direction is not supported by this codec
     */
    error_t (*get_input_gain_multiplier)(struct Device* device, float* gain);
};

/** @brief See AudioCodecApi::open */
error_t audio_codec_open(struct Device* device, const struct AudioCodecStreamConfig* config);

/** @brief See AudioCodecApi::close */
error_t audio_codec_close(struct Device* device);

/** @brief See AudioCodecApi::read */
error_t audio_codec_read(struct Device* device, void* data, size_t data_size, size_t* bytes_read, TickType_t timeout);

/** @brief See AudioCodecApi::write */
error_t audio_codec_write(struct Device* device, const void* data, size_t data_size, size_t* bytes_written, TickType_t timeout);

/** @brief See AudioCodecApi::set_volume */
error_t audio_codec_set_volume(struct Device* device, enum AudioCodecDirection direction, float volume_percent);

/** @brief See AudioCodecApi::get_volume */
error_t audio_codec_get_volume(struct Device* device, enum AudioCodecDirection direction, float* volume_percent);

/** @brief See AudioCodecApi::set_mute */
error_t audio_codec_set_mute(struct Device* device, enum AudioCodecDirection direction, bool muted);

/** @brief See AudioCodecApi::get_mute */
error_t audio_codec_get_mute(struct Device* device, enum AudioCodecDirection direction, bool* muted);

/** @brief See AudioCodecApi::get_native_sample_rate */
error_t audio_codec_get_native_sample_rate(struct Device* device, enum AudioCodecDirection direction, uint32_t* rate_hz);

/** @brief See AudioCodecApi::get_native_channels */
error_t audio_codec_get_native_channels(struct Device* device, enum AudioCodecDirection direction, uint8_t* channels);

/** @brief See AudioCodecApi::get_capabilities */
error_t audio_codec_get_capabilities(struct Device* device, enum AudioCodecDirection* supported_directions);

/**
 * @brief See AudioCodecApi::get_input_gain_multiplier. Returns 1.0 (no change) if the
 * codec doesn't implement this field, rather than ERROR_NOT_SUPPORTED -- callers can treat
 * the multiplier as always present.
 */
error_t audio_codec_get_input_gain_multiplier(struct Device* device, float* gain);

extern const struct DeviceType AUDIO_CODEC_TYPE;

#ifdef __cplusplus
}
#endif
