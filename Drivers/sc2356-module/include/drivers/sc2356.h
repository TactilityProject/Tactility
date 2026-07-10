// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <tactility/drivers/camera.h>
#include <tactility/error.h>

struct Device;

#ifdef __cplusplus
extern "C" {
#endif

struct Sc2356Config {
    /** SCCB I2C address (0x36) */
    uint8_t address;
};

/**
 * Opaque camera handle returned by sc2356_open().
 * Not thread-safe: the capture flow (sc2356_get_frame/sc2356_capture_jpeg + sc2356_release_frame)
 * must be driven from a single consumer at a time. state->last_dqbuf_index and the shared PPA
 * output buffer are not guarded by a lock, so overlapping calls from multiple tasks on the same
 * handle will race.
 */
typedef CameraHandle Sc2356Handle;

/**
 * Initialize the esp_video subsystem and open the MIPI CSI video device.
 * Reuses the parent I2C controller's bus handle for SCCB communication.
 * Must be called before any other sc2356_* functions.
 * @param device  the sc2356 device from the device tree
 * @param handle  out: opaque handle to pass to subsequent calls
 * @return ERROR_NONE on success
 */
error_t sc2356_open(struct Device* device, Sc2356Handle* handle);

/**
 * Stop streaming, unmap frame buffers, and release all resources.
 * @param handle  handle returned by sc2356_open()
 * @return ERROR_NONE on success
 */
error_t sc2356_close(Sc2356Handle handle);

/**
 * Dequeue one RGB565 frame. Blocks until a frame is available or the timeout expires.
 * The caller must call sc2356_release_frame() to return the buffer to the camera queue.
 * @param handle      handle returned by sc2356_open()
 * @param buf         out: pointer to the DMA-mapped RGB565 frame buffer
 * @param len         out: byte length of buf (width * height * 2)
 * @param timeout_ms  maximum time to wait in milliseconds
 * @param out_width   optional out: width of buf at the moment it was produced, atomic with the frame data (may be NULL)
 * @param out_height  optional out: height of buf at the moment it was produced, atomic with the frame data (may be NULL)
 * @return ERROR_NONE on success, ERROR_TIMEOUT if no frame arrived in time
 */
error_t sc2356_get_frame(Sc2356Handle handle, uint8_t** buf, size_t* len, uint32_t timeout_ms, uint32_t* out_width, uint32_t* out_height);

/**
 * Return the last dequeued frame buffer to the V4L2 capture queue.
 * Must be called after each successful sc2356_get_frame().
 * @param handle  handle returned by sc2356_open()
 * @return ERROR_NONE on success
 */
error_t sc2356_release_frame(Sc2356Handle handle);

/**
 * Return the frame width in pixels (1280 for the default 720p config).
 * @param handle  handle returned by sc2356_open()
 */
uint32_t sc2356_get_width(Sc2356Handle handle);

/**
 * Return the frame height in pixels (720 for the default 720p config).
 * @param handle  handle returned by sc2356_open()
 */
uint32_t sc2356_get_height(Sc2356Handle handle);

/**
 * Change the clockwise rotation applied to subsequent frames.
 * Can be called at any time while the handle is open, including during streaming.
 * @param handle    handle returned by sc2356_open()
 * @param rotation  new rotation
 * @return ERROR_NONE on success
 */
error_t sc2356_set_rotation(Sc2356Handle handle, CameraRotation rotation);

/**
 * Capture one frame and JPEG-encode it using the hardware JPEG encoder.
 * Allocates output buffer in SPIRAM; caller must free it with heap_caps_free().
 * @param handle     handle returned by sc2356_open()
 * @param out_buf    out: pointer to JPEG-encoded data (caller must free)
 * @param out_len    out: byte length of JPEG data
 * @param quality    JPEG quality 1-100
 * @return ERROR_NONE on success
 */
error_t sc2356_capture_jpeg(Sc2356Handle handle, uint8_t** out_buf, size_t* out_len, uint8_t quality);

#ifdef __cplusplus
}
#endif
