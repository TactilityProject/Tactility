// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/audio_codec.h>
#include <tactility/error.h>
#include <tactility/device.h>

#define AUDIO_CODEC_DRIVER_API(driver) ((struct AudioCodecApi*)driver->api)

extern "C" {

error_t audio_codec_open(Device* device, const struct AudioCodecStreamConfig* config) {
    const auto* driver = device_get_driver(device);
    return AUDIO_CODEC_DRIVER_API(driver)->open(device, config);
}

error_t audio_codec_close(Device* device) {
    const auto* driver = device_get_driver(device);
    return AUDIO_CODEC_DRIVER_API(driver)->close(device);
}

error_t audio_codec_read(Device* device, void* data, size_t data_size, size_t* bytes_read, TickType_t timeout) {
    const auto* driver = device_get_driver(device);
    return AUDIO_CODEC_DRIVER_API(driver)->read(device, data, data_size, bytes_read, timeout);
}

error_t audio_codec_write(Device* device, const void* data, size_t data_size, size_t* bytes_written, TickType_t timeout) {
    const auto* driver = device_get_driver(device);
    return AUDIO_CODEC_DRIVER_API(driver)->write(device, data, data_size, bytes_written, timeout);
}

error_t audio_codec_set_volume(Device* device, AudioCodecDirection direction, float volume_percent) {
    const auto* driver = device_get_driver(device);
    return AUDIO_CODEC_DRIVER_API(driver)->set_volume(device, direction, volume_percent);
}

error_t audio_codec_get_volume(Device* device, AudioCodecDirection direction, float* volume_percent) {
    const auto* driver = device_get_driver(device);
    return AUDIO_CODEC_DRIVER_API(driver)->get_volume(device, direction, volume_percent);
}

error_t audio_codec_set_mute(Device* device, AudioCodecDirection direction, bool muted) {
    const auto* driver = device_get_driver(device);
    return AUDIO_CODEC_DRIVER_API(driver)->set_mute(device, direction, muted);
}

error_t audio_codec_get_mute(Device* device, AudioCodecDirection direction, bool* muted) {
    const auto* driver = device_get_driver(device);
    return AUDIO_CODEC_DRIVER_API(driver)->get_mute(device, direction, muted);
}

error_t audio_codec_get_native_sample_rate(Device* device, AudioCodecDirection direction, uint32_t* rate_hz) {
    const auto* driver = device_get_driver(device);
    return AUDIO_CODEC_DRIVER_API(driver)->get_native_sample_rate(device, direction, rate_hz);
}

error_t audio_codec_get_native_channels(Device* device, AudioCodecDirection direction, uint8_t* channels) {
    const auto* driver = device_get_driver(device);
    return AUDIO_CODEC_DRIVER_API(driver)->get_native_channels(device, direction, channels);
}

error_t audio_codec_get_capabilities(Device* device, AudioCodecDirection* supported_directions) {
    const auto* driver = device_get_driver(device);
    return AUDIO_CODEC_DRIVER_API(driver)->get_capabilities(device, supported_directions);
}

error_t audio_codec_get_input_gain_multiplier(Device* device, float* gain) {
    const auto* driver = device_get_driver(device);
    if (AUDIO_CODEC_DRIVER_API(driver)->get_input_gain_multiplier == nullptr) {
        *gain = 1.0f;
        return ERROR_NONE;
    }
    return AUDIO_CODEC_DRIVER_API(driver)->get_input_gain_multiplier(device, gain);
}

const struct DeviceType AUDIO_CODEC_TYPE {
    .name = "audio-codec"
};

}
