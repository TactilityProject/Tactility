// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <tactility/drivers/camera.h>
#include <tactility/drivers/gpio.h>
#include <tactility/error.h>

struct Device;

#ifdef __cplusplus
extern "C" {
#endif

struct Gc0308Config {
    /** SCCB I2C address (0x21) */
    uint8_t address;

    /** Clockwise mounting correction applied after the requested display rotation. */
    uint16_t rotation_offset;

    uint32_t xclk_frequency_hz;
    struct GpioPinSpec pin_d0;
    struct GpioPinSpec pin_d1;
    struct GpioPinSpec pin_d2;
    struct GpioPinSpec pin_d3;
    struct GpioPinSpec pin_d4;
    struct GpioPinSpec pin_d5;
    struct GpioPinSpec pin_d6;
    struct GpioPinSpec pin_d7;
    struct GpioPinSpec pin_vsync;
    struct GpioPinSpec pin_de;
    struct GpioPinSpec pin_pclk;
    struct GpioPinSpec pin_xclk;

    // Reset pin. GPIO_PIN_SPEC_NONE if the sensor's reset is tied high on the board (or otherwise
    // not under our control), in which case start_device skips the reset pulse entirely and goes
    // straight to probing.
    struct GpioPinSpec pin_reset;
};

/**
 * Opaque camera handle returned by gc0308_open().
 * Not thread-safe: the capture flow (gc0308_get_frame/gc0308_capture_jpeg + gc0308_release_frame)
 * must be driven from a single consumer at a time.
 */
typedef CameraHandle Gc0308Handle;

/**
 * Initialize the esp_video DVP subsystem and open the DVP video device.
 * Reuses the parent I2C controller's bus handle for SCCB communication.
 * Must be called before any other gc0308_* functions.
 * @param device  the gc0308 device from the device tree
 * @param handle  out: opaque handle to pass to subsequent calls
 * @return ERROR_NONE on success
 */
error_t gc0308_open(struct Device* device, Gc0308Handle* handle);

/**
 * Stop streaming, unmap frame buffers, and release all resources.
 * @param handle  handle returned by gc0308_open()
 * @return ERROR_NONE on success
 */
error_t gc0308_close(Gc0308Handle handle);

/**
 * Dequeue one RGB565 frame. Blocks until a frame is available or the timeout expires.
 * The caller must call gc0308_release_frame() to return the buffer to the camera queue.
 * @param handle      handle returned by gc0308_open()
 * @param buf         out: pointer to the DMA-mapped RGB565 frame buffer
 * @param len         out: byte length of buf (width * height * 2)
 * @param timeout_ms  maximum time to wait in milliseconds
 * @param out_width   optional out: width of buf at the moment it was produced, atomic with the frame data (may be NULL)
 * @param out_height  optional out: height of buf at the moment it was produced, atomic with the frame data (may be NULL)
 * @return ERROR_NONE on success, ERROR_TIMEOUT if no frame arrived in time
 */
error_t gc0308_get_frame(Gc0308Handle handle, uint8_t** buf, size_t* len, uint32_t timeout_ms, uint32_t* out_width, uint32_t* out_height);

/**
 * Return the last dequeued frame buffer to the V4L2 capture queue.
 * Must be called after each successful gc0308_get_frame().
 * @param handle  handle returned by gc0308_open()
 * @return ERROR_NONE on success
 */
error_t gc0308_release_frame(Gc0308Handle handle);

/**
 * Return the frame width in pixels.
 * @param handle  handle returned by gc0308_open()
 */
uint32_t gc0308_get_width(Gc0308Handle handle);

/**
 * Return the frame height in pixels.
 * @param handle  handle returned by gc0308_open()
 */
uint32_t gc0308_get_height(Gc0308Handle handle);

/**
 * Set the requested clockwise rotation. Subsequent frames are software-rotated
 * and their reported dimensions reflect the requested orientation.
 * Can be called at any time while the handle is open, including during streaming.
 * @param handle    handle returned by gc0308_open()
 * @param rotation  new rotation
 * @return ERROR_NONE on success
 */
error_t gc0308_set_rotation(Gc0308Handle handle, CameraRotation rotation);

/**
 * Capture one frame and JPEG-encode it using the hardware JPEG encoder.
 * Allocates output buffer in SPIRAM; caller must free it with heap_caps_free().
 * @param handle     handle returned by gc0308_open()
 * @param out_buf    out: pointer to JPEG-encoded data (caller must free)
 * @param out_len    out: byte length of JPEG data
 * @param quality    JPEG quality 1-100
 * @return ERROR_NONE on success
 */
error_t gc0308_capture_jpeg(Gc0308Handle handle, uint8_t** out_buf, size_t* out_len, uint8_t quality);

#ifdef __cplusplus
}
#endif
