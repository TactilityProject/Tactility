// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <tactility/device.h>
#include <tactility/error.h>

/**
 * @brief Pixel color formats supported by display panel drivers.
 */
enum DisplayColorFormat {
    DISPLAY_COLOR_FORMAT_MONOCHROME = 0x0, // 1 bpp
    DISPLAY_COLOR_FORMAT_BGR565 = 0x1,
    DISPLAY_COLOR_FORMAT_BGR565_SWAPPED = 0x2,
    DISPLAY_COLOR_FORMAT_RGB565 = 0x3,
    DISPLAY_COLOR_FORMAT_RGB565_SWAPPED = 0x4,
    DISPLAY_COLOR_FORMAT_RGB888 = 0x5,
};

/**
 * @brief API for display panel drivers.
 */
struct DisplayApi {
    /**
     * @brief Performs a hardware reset of the panel (e.g. toggling a reset GPIO).
     * @param[in] device the display device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*reset)(struct Device* device);

    /**
     * @brief Sends the panel's initialization command sequence. Call after reset().
     * @param[in] device the display device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*init)(struct Device* device);

    /**
     * @brief Draws pixel data into the given rectangle.
     * @param[in] device the display device
     * @param[in] x_start left edge (inclusive)
     * @param[in] y_start top edge (inclusive)
     * @param[in] x_end right edge (exclusive)
     * @param[in] y_end bottom edge (exclusive)
     * @param[in] color_data pixel data in the panel's configured color format
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*draw_bitmap)(struct Device* device, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data);

    /**
     * @brief Mirrors the image along the X and/or Y axis.
     * @param[in] device the display device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*mirror)(struct Device* device, bool x_axis, bool y_axis);

    /**
     * @brief Swaps the X and Y axes (rotates the coordinate system).
     * @param[in] device the display device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*swap_xy)(struct Device* device, bool swap_axes);

    /**
     * @brief Sets a coordinate offset applied to all draw operations.
     * @param[in] device the display device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*set_gap)(struct Device* device, int32_t x_gap, int32_t y_gap);

    /**
     * @brief Inverts the panel's color output.
     * @param[in] device the display device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*invert_color)(struct Device* device, bool invert_color_data);

    /**
     * @brief Turns the panel's display output on or off.
     * @param[in] device the display device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*disp_on_off)(struct Device* device, bool on_off);

    /**
     * @brief Puts the panel into or out of its low-power sleep mode.
     * @param[in] device the display device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*disp_sleep)(struct Device* device, bool sleep);

    /**
     * @brief Gets the panel's pixel color format.
     * @param[in] device the display device
     * @return the color format
     */
    enum DisplayColorFormat (*get_color_format)(struct Device* device);

    /**
     * @brief Gets the panel's horizontal resolution in pixels.
     * @param[in] device the display device
     * @return the horizontal resolution
     */
    uint16_t (*get_resolution_x)(struct Device* device);

    /**
     * @brief Gets the panel's vertical resolution in pixels.
     * @param[in] device the display device
     * @return the vertical resolution
     */
    uint16_t (*get_resolution_y)(struct Device* device);

    /**
     * @brief Gets a pointer to one of the panel's frame buffers, for panels that expose direct frame buffer access.
     * @param[in] device the display device
     * @param[in] index the frame buffer index (see get_frame_buffer_count())
     * @param[out] out_buffer the buffer pointer
     */
    void (*get_frame_buffer)(struct Device* device, uint8_t index, void** out_buffer);

    /**
     * @brief Gets the number of frame buffers exposed by the panel via get_frame_buffer(). 0 when unsupported.
     * @param[in] device the display device
     * @return the frame buffer count
     */
    uint8_t (*get_frame_buffer_count)(struct Device* device);
};

/**
 * @brief Performs a hardware reset of the panel using the specified display.
 */
error_t display_reset(struct Device* device);

/**
 * @brief Sends the panel's initialization command sequence using the specified display.
 */
error_t display_init(struct Device* device);

/**
 * @brief Draws pixel data into the given rectangle using the specified display.
 */
error_t display_draw_bitmap(struct Device* device, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data);

/**
 * @brief Mirrors the image along the X and/or Y axis using the specified display.
 */
error_t display_mirror(struct Device* device, bool x_axis, bool y_axis);

/**
 * @brief Swaps the X and Y axes using the specified display.
 */
error_t display_swap_xy(struct Device* device, bool swap_axes);

/**
 * @brief Sets a coordinate offset applied to all draw operations using the specified display.
 */
error_t display_set_gap(struct Device* device, int32_t x_gap, int32_t y_gap);

/**
 * @brief Inverts the panel's color output using the specified display.
 */
error_t display_invert_color(struct Device* device, bool invert_color_data);

/**
 * @brief Turns the panel's display output on or off using the specified display.
 */
error_t display_disp_on_off(struct Device* device, bool on_off);

/**
 * @brief Puts the panel into or out of its low-power sleep mode using the specified display.
 */
error_t display_disp_sleep(struct Device* device, bool sleep);

/**
 * @brief Gets the pixel color format using the specified display.
 */
enum DisplayColorFormat display_get_color_format(struct Device* device);

/**
 * @brief Gets the horizontal resolution in pixels using the specified display.
 */
uint16_t display_get_resolution_x(struct Device* device);

/**
 * @brief Gets the vertical resolution in pixels using the specified display.
 */
uint16_t display_get_resolution_y(struct Device* device);

/**
 * @brief Gets a pointer to one of the panel's frame buffers using the specified display.
 */
void display_get_frame_buffer(struct Device* device, uint8_t index, void** out_buffer);

/**
 * @brief Gets the number of frame buffers exposed by the panel using the specified display.
 */
uint8_t display_get_frame_buffer_count(struct Device* device);

extern const struct DeviceType DISPLAY_TYPE;

#ifdef __cplusplus
}
#endif
