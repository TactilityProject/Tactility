// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/audio_stream.h>
#include <tactility/error.h>
#include <tactility/device.h>

#define AUDIO_STREAM_DRIVER_API(driver) ((struct AudioStreamApi*)driver->api)

extern "C" {

error_t audio_stream_open_input(Device* device, const struct AudioStreamConfig* config, AudioStreamHandle* outHandle) {
    const auto* driver = device_get_driver(device);
    return AUDIO_STREAM_DRIVER_API(driver)->open_input(device, config, outHandle);
}

error_t audio_stream_open_output(Device* device, const struct AudioStreamConfig* config, AudioStreamHandle* outHandle) {
    const auto* driver = device_get_driver(device);
    return AUDIO_STREAM_DRIVER_API(driver)->open_output(device, config, outHandle);
}

error_t audio_stream_read(AudioStreamHandle handle, void* data, size_t dataSize, size_t* bytesRead, TickType_t timeout) {
    const auto* driver = device_get_driver(handle->device);
    return AUDIO_STREAM_DRIVER_API(driver)->read(handle, data, dataSize, bytesRead, timeout);
}

error_t audio_stream_write(AudioStreamHandle handle, const void* data, size_t dataSize, size_t* bytesWritten, TickType_t timeout) {
    const auto* driver = device_get_driver(handle->device);
    return AUDIO_STREAM_DRIVER_API(driver)->write(handle, data, dataSize, bytesWritten, timeout);
}

error_t audio_stream_close(AudioStreamHandle handle) {
    const auto* driver = device_get_driver(handle->device);
    return AUDIO_STREAM_DRIVER_API(driver)->close(handle);
}

error_t audio_stream_set_volume(Device* device, enum AudioCodecDirection direction, float volumePercent) {
    const auto* driver = device_get_driver(device);
    return AUDIO_STREAM_DRIVER_API(driver)->set_volume(device, direction, volumePercent);
}

error_t audio_stream_get_volume(Device* device, enum AudioCodecDirection direction, float* volumePercent) {
    const auto* driver = device_get_driver(device);
    return AUDIO_STREAM_DRIVER_API(driver)->get_volume(device, direction, volumePercent);
}

error_t audio_stream_set_mute(Device* device, enum AudioCodecDirection direction, bool muted) {
    const auto* driver = device_get_driver(device);
    return AUDIO_STREAM_DRIVER_API(driver)->set_mute(device, direction, muted);
}

error_t audio_stream_get_mute(Device* device, enum AudioCodecDirection direction, bool* muted) {
    const auto* driver = device_get_driver(device);
    return AUDIO_STREAM_DRIVER_API(driver)->get_mute(device, direction, muted);
}

error_t audio_stream_set_enabled(Device* device, enum AudioCodecDirection direction, bool enabled) {
    const auto* driver = device_get_driver(device);
    return AUDIO_STREAM_DRIVER_API(driver)->set_enabled(device, direction, enabled);
}

error_t audio_stream_get_enabled(Device* device, enum AudioCodecDirection direction, bool* enabled) {
    const auto* driver = device_get_driver(device);
    return AUDIO_STREAM_DRIVER_API(driver)->get_enabled(device, direction, enabled);
}

error_t audio_stream_is_supported(Device* device, enum AudioCodecDirection direction, bool* supported) {
    const auto* driver = device_get_driver(device);
    return AUDIO_STREAM_DRIVER_API(driver)->is_supported(device, direction, supported);
}

error_t audio_stream_set_change_callback(Device* device, AudioStreamChangeCallback callback, void* userData) {
    const auto* driver = device_get_driver(device);
    return AUDIO_STREAM_DRIVER_API(driver)->set_change_callback(device, callback, userData);
}

const struct DeviceType AUDIO_STREAM_TYPE {
    .name = "audio-stream"
};

}
