// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/camera.h>
#include <tactility/error.h>
#include <tactility/device.h>

#define CAMERA_DRIVER_API(driver) ((struct CameraApi*)driver->api)

extern "C" {

error_t camera_open(Device* device, CameraHandle* out_handle) {
    const auto* driver = device_get_driver(device);
    return CAMERA_DRIVER_API(driver)->open(device, out_handle);
}

error_t camera_close(CameraHandle handle) {
    const auto* driver = device_get_driver(handle->device);
    return CAMERA_DRIVER_API(driver)->close(handle);
}

error_t camera_get_frame(CameraHandle handle, uint8_t** buf, size_t* len, uint32_t timeout_ms, uint32_t* out_width, uint32_t* out_height) {
    const auto* driver = device_get_driver(handle->device);
    return CAMERA_DRIVER_API(driver)->get_frame(handle, buf, len, timeout_ms, out_width, out_height);
}

error_t camera_release_frame(CameraHandle handle) {
    const auto* driver = device_get_driver(handle->device);
    return CAMERA_DRIVER_API(driver)->release_frame(handle);
}

uint32_t camera_get_width(CameraHandle handle) {
    const auto* driver = device_get_driver(handle->device);
    return CAMERA_DRIVER_API(driver)->get_width(handle);
}

uint32_t camera_get_height(CameraHandle handle) {
    const auto* driver = device_get_driver(handle->device);
    return CAMERA_DRIVER_API(driver)->get_height(handle);
}

error_t camera_set_rotation(CameraHandle handle, CameraRotation rotation) {
    const auto* driver = device_get_driver(handle->device);
    return CAMERA_DRIVER_API(driver)->set_rotation(handle, rotation);
}

error_t camera_capture_jpeg(CameraHandle handle, uint8_t** out_buf, size_t* out_len, uint8_t quality) {
    const auto* driver = device_get_driver(handle->device);
    return CAMERA_DRIVER_API(driver)->capture_jpeg(handle, out_buf, out_len, quality);
}

const struct DeviceType CAMERA_TYPE {
    .name = "camera"
};

}
