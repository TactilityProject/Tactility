// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <tactility/device.h>
#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Handle to an open camera stream (see camera_open).
 *
 * The `device` field identifies the owning camera device so that camera_get_frame/
 * camera_close/etc. can dispatch to the correct driver; implementations embed this as
 * the first member (C) or base class (C++) of a larger private struct and may store
 * additional state after it.
 */
struct CameraHandleData {
    struct Device* device;
};

typedef struct CameraHandleData* CameraHandle;

/** Clockwise rotation applied to frames */
typedef enum {
    CAMERA_ROTATION_0   = 0,
    CAMERA_ROTATION_90  = 90,
    CAMERA_ROTATION_180 = 180,
    CAMERA_ROTATION_270 = 270,
} CameraRotation;

/**
 * @brief API for camera drivers.
 */
struct CameraApi {
    /**
     * @brief Opens the camera and starts streaming.
     * @param[in] device the camera device
     * @param[out] out_handle receives the opened camera handle
     * @return ERROR_NONE on success
     */
    error_t (*open)(struct Device* device, CameraHandle* out_handle);

    /**
     * @brief Stops streaming and releases all resources associated with the handle.
     * @param[in] handle handle returned by open
     * @return ERROR_NONE on success
     */
    error_t (*close)(CameraHandle handle);

    /**
     * @brief Dequeues one RGB565 frame. Blocks until a frame is available or the timeout expires.
     * The caller must call release_frame to return the buffer to the camera queue.
     * @param[in] handle handle returned by open
     * @param[out] buf pointer to the frame buffer
     * @param[out] len byte length of buf
     * @param[in] timeout_ms maximum time to wait in milliseconds
     * @param[out] out_width optional: width of buf at the moment it was produced (may be NULL)
     * @param[out] out_height optional: height of buf at the moment it was produced (may be NULL)
     * @retval ERROR_NONE on success
     * @retval ERROR_TIMEOUT if no frame arrived in time
     */
    error_t (*get_frame)(CameraHandle handle, uint8_t** buf, size_t* len, uint32_t timeout_ms, uint32_t* out_width, uint32_t* out_height);

    /**
     * @brief Returns the last dequeued frame buffer to the capture queue.
     * Must be called after each successful get_frame.
     * @param[in] handle handle returned by open
     * @return ERROR_NONE on success
     */
    error_t (*release_frame)(CameraHandle handle);

    /** @brief Returns the current frame width in pixels. */
    uint32_t (*get_width)(CameraHandle handle);

    /** @brief Returns the current frame height in pixels. */
    uint32_t (*get_height)(CameraHandle handle);

    /**
     * @brief Changes the clockwise rotation applied to subsequent frames.
     * Can be called at any time while the handle is open, including during streaming.
     * @param[in] handle handle returned by open
     * @param[in] rotation new rotation
     * @return ERROR_NONE on success
     */
    error_t (*set_rotation)(CameraHandle handle, CameraRotation rotation);

    /**
     * @brief Captures one frame and JPEG-encodes it.
     * @param[in] handle handle returned by open
     * @param[out] out_buf pointer to JPEG-encoded data (caller must free with heap_caps_free)
     * @param[out] out_len byte length of JPEG data
     * @param[in] quality JPEG quality 1-100
     * @return ERROR_NONE on success
     */
    error_t (*capture_jpeg)(CameraHandle handle, uint8_t** out_buf, size_t* out_len, uint8_t quality);
};

/** @brief See CameraApi::open */
error_t camera_open(struct Device* device, CameraHandle* out_handle);

/** @brief See CameraApi::close */
error_t camera_close(CameraHandle handle);

/** @brief See CameraApi::get_frame */
error_t camera_get_frame(CameraHandle handle, uint8_t** buf, size_t* len, uint32_t timeout_ms, uint32_t* out_width, uint32_t* out_height);

/** @brief See CameraApi::release_frame */
error_t camera_release_frame(CameraHandle handle);

/** @brief See CameraApi::get_width */
uint32_t camera_get_width(CameraHandle handle);

/** @brief See CameraApi::get_height */
uint32_t camera_get_height(CameraHandle handle);

/** @brief See CameraApi::set_rotation */
error_t camera_set_rotation(CameraHandle handle, CameraRotation rotation);

/** @brief See CameraApi::capture_jpeg */
error_t camera_capture_jpeg(CameraHandle handle, uint8_t** out_buf, size_t* out_len, uint8_t quality);

extern const struct DeviceType CAMERA_TYPE;

#ifdef __cplusplus
}
#endif
