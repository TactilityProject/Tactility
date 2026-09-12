// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <tactility/device.h>
#include <tactility/error.h>

/**
 * @brief Optional capabilities of the display.
 */
enum DisplayCapability {
    DISPLAY_CAPABILITY_CAP_MIRROR = 1 << 0,
    DISPLAY_CAPABILITY_CAP_SWAP_XY = 1 << 1,
    DISPLAY_CAPABILITY_CAP_SET_GAP = 1 << 2,
    DISPLAY_CAPABILITY_INVERT_COLOR = 1 << 3,
    DISPLAY_CAPABILITY_ON_OFF = 1 << 4,
    DISPLAY_CAPABILITY_BACKLIGHT = 1 << 5,
    DISPLAY_CAPABILITY_SLEEP = 1 << 6,
    DISPLAY_CAPABILITY_REQUIRES_FULL_FRAME = 1 << 7,
    /** Can be used by e-paper with pointer devices for long click timing changes. */
    DISPLAY_CAPABILITY_SLOW_REFRESH = 1 << 8,
    /**
     * Promise that draw_bitmap() never DMAs directly from the color_data pointer it's given (e.g.
     * it copies/converts into its own buffer first). Lets the LVGL bridge allocate this display's
     * draw buffer(s) from non-DMA-capable memory instead of forcing scarce internal RAM.
     */
    DISPLAY_CAPABILITY_PREFER_EXTERNAL_RAM = 1 << 9
};

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
    // 8 bpp luminance, 0x00 = black, 0xFF = white (matches LVGL's LV_COLOR_FORMAT_L8). Unlike
    // MONOCHROME, the LVGL bridge does not force full-frame rendering for this format, so drivers
    // that want real partial/tile updates (e.g. grayscale e-paper panels) should report this
    // instead of MONOCHROME even if they intend to threshold down to black/white themselves.
    DISPLAY_COLOR_FORMAT_GRAYSCALE8 = 0x6,
};

/**
 * @brief API for display panel drivers.
 */
struct DisplayApi {
    uint32_t capabilities;

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
     * @brief Fully wipes/resets the panel's visible image, independent of anything previously
     * drawn via draw_bitmap(). Intended for panels where content drawn outside the normal
     * screen/widget model, such as an app writing directly via draw_bitmap(), can leave residue
     * that nothing else knows to repaint. E-paper ghosting is the main case.
     * @warning Nullable. When null, this display has no dedicated clear operation. Callers should
     * fall back to redrawing (e.g. a full-frame draw_bitmap()) if a clear is needed.
     * @param[in] device the display device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*clear)(struct Device* device);

    /**
     * @brief Forces a full-quality redraw of whatever is already in the panel's buffer, without
     * changing its content. Unlike clear(), which wipes the image first, this is for e-paper
     * panels doing a GC16-equivalent pass to clear ghosting left by earlier fast or partial
     * draw_bitmap() calls, as periodic maintenance while content is otherwise correct but has
     * accumulated visible residue from the panel's own display technology.
     * @warning Nullable. When null, this display has no such maintenance pass.
     * @param[in] device the display device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*refresh)(struct Device* device);

    /**
     * @brief Mirrors the image along the X and/or Y axis.
     * @warning Function pointer should be null if capability not available.
     * @param[in] device the display device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*mirror)(struct Device* device, bool x_axis, bool y_axis);

    /**
     * @brief Swaps the X and Y axes (rotates the coordinate system).
     * @warning Function pointer should be null if capability not available.
     * @param[in] device the display device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*swap_xy)(struct Device* device, bool swap_axes);

    /**
     * @brief Gets whether the X and Y axes are currently swapped.
     * @warning Function pointer should be null if capability not available.
     * @param[in] device the display device
     * @return true if swapped
     */
    bool (*get_swap_xy)(struct Device* device);

    /**
     * @brief Gets whether the X axis is currently mirrored.
     * @warning Function pointer should be null if capability not available.
     * @param[in] device the display device
     * @return true if mirrored
     */
    bool (*get_mirror_x)(struct Device* device);

    /**
     * @brief Gets whether the Y axis is currently mirrored.
     * @warning Function pointer should be null if capability not available.
     * @param[in] device the display device
     * @return true if mirrored
     */
    bool (*get_mirror_y)(struct Device* device);

    /**
     * @brief Sets a coordinate offset applied to all draw operations.
     * @warning Function pointer should be null if capability not available.
     * @param[in] device the display device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*set_gap)(struct Device* device, int32_t x_gap, int32_t y_gap);

    /**
     * @brief Gets the devicetree-configured baseline X gap (i.e. as set at LV_DISPLAY_ROTATION_0,
     * before any rotation-driven swap_xy/mirror changes) - not necessarily the last value passed
     * to set_gap(), which callers (e.g. lvgl_display.c on rotation change) may have since swapped.
     * @warning Function pointer should be null if capability not available.
     * @param[in] device the display device
     * @return the baseline X gap
     */
    int32_t (*get_gap_x)(struct Device* device);

    /**
     * @brief Gets the devicetree-configured baseline Y gap. See get_gap_x() for details.
     * @warning Function pointer should be null if capability not available.
     * @param[in] device the display device
     * @return the baseline Y gap
     */
    int32_t (*get_gap_y)(struct Device* device);

    /**
     * @brief Inverts the panel's color output.
     * @warning Function pointer should be null if capability not available.
     * @param[in] device the display device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*invert_color)(struct Device* device, bool invert_color_data);

    /**
     * @brief Turns the panel's display output on or off.
     * @warning Function pointer should be null if capability not available.
     * @param[in] device the display device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*disp_on_off)(struct Device* device, bool on_off);

    /**
     * @brief Puts the panel into or out of its low-power sleep mode.
     * @warning Function pointer should be null if capability not available.
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

    /**
     * @brief Gets the backlight device associated with this display, if any.
     * @warning Function pointer should be null if capability not available.
     * @warning Implementations must not take a reference on *backlight: display_get_backlight()
     * adds it on their behalf, once, so drivers don't each have to remember to.
     * @param[in] device the display device
     * @param[out] backlight the associated backlight device
     * @retval ERROR_NONE when a backlight is available and *backlight was set
     * @retval ERROR_NOT_SUPPORTED when this display has no associated backlight
     */
    error_t (*get_backlight)(struct Device* device, struct Device** backlight);

    /**
     * @brief Optional dynamic override for capability checks.
     * @warning Nullable. When null, display_has_capability() checks the static `capabilities` field
     * instead. When set, this is called in its place, letting a driver report capabilities that vary
     * per device instance (e.g. depending on devicetree config) rather than being fixed for the driver.
     * @param[in] device the display device
     * @param[in] capability the bitset of DisplayCapability value(s) being queried
     * @return true if all specified capabilities are available for this device instance
     */
    bool (*has_capability)(struct Device* device, uint32_t capability);
};

/**
 * @brief Check if this display has 1 or more capabilities.
 * @param device the display device
 * @param capability the bitset of DisplayCapability value(s)
 * @return true if all specified capabilities are available
 */
bool display_has_capability(struct Device* device, uint32_t capability);

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
 * @brief Fully wipes/resets the panel's visible image using the specified display. See
 * DisplayApi::clear() for details.
 * @retval ERROR_NOT_SUPPORTED when this display has no clear() implementation
 */
error_t display_clear(struct Device* device);

/**
 * @brief Forces a full-quality redraw of the panel's current content using the specified display.
 * See DisplayApi::refresh() for details.
 * @retval ERROR_NOT_SUPPORTED when this display has no refresh() implementation
 */
error_t display_refresh(struct Device* device);

/**
 * @brief Mirrors the image along the X and/or Y axis using the specified display.
 */
error_t display_mirror(struct Device* device, bool x_axis, bool y_axis);

/**
 * @brief Swaps the X and Y axes using the specified display.
 */
error_t display_swap_xy(struct Device* device, bool swap_axes);

/**
 * @brief Gets whether the X and Y axes are currently swapped using the specified display.
 */
bool display_get_swap_xy(struct Device* device);

/**
 * @brief Gets whether the X axis is currently mirrored using the specified display.
 */
bool display_get_mirror_x(struct Device* device);

/**
 * @brief Gets whether the Y axis is currently mirrored using the specified display.
 */
bool display_get_mirror_y(struct Device* device);

/**
 * @brief Sets a coordinate offset applied to all draw operations using the specified display.
 */
error_t display_set_gap(struct Device* device, int32_t x_gap, int32_t y_gap);

/**
 * @brief Gets the devicetree-configured baseline X gap using the specified display. See
 * DisplayApi::get_gap_x() for details.
 */
int32_t display_get_gap_x(struct Device* device);

/**
 * @brief Gets the devicetree-configured baseline Y gap using the specified display. See
 * DisplayApi::get_gap_y() for details.
 */
int32_t display_get_gap_y(struct Device* device);

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

/**
 * @brief Gets the backlight device associated with the specified display, if any.
 * @warning On success, *backlight is returned with an added reference (see device_get()).
 * The caller must call device_put() on it once done.
 * @retval ERROR_NONE when a backlight is available and *backlight was set
 * @retval ERROR_NOT_SUPPORTED when the display has no associated backlight
 */
error_t display_get_backlight(struct Device* device, struct Device** backlight);

extern const struct DeviceType DISPLAY_TYPE;

#ifdef __cplusplus
}
#endif
