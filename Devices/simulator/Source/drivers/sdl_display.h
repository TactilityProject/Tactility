// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SDL2/SDL.h>

struct Device;

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct SdlDisplayConfig {
    uint16_t horizontal_resolution;
    uint16_t vertical_resolution;
    /** Frames are 8 bit luminance instead of RGB565 */
    bool grayscale;
};

/**
 * @return the display's renderer, or NULL if the display hasn't drawn its first frame yet
 *     (see sdl_display_lazy_init() in sdl_display.cpp). Used by sdl_input.cpp to map window
 *     coordinates back to the fixed logical resolution the renderer scales to fit the window.
 */
SDL_Renderer* sdl_display_get_renderer(void);

/**
 * @brief Gets the size of the oriented view that the window shows: the display's resolution, with the width and
 *     height swapped while swap_xy is on. It follows a rotation right away, before the window is redrawn.
 * @retval false when there is no display
 */
bool sdl_display_get_view_size(int* width, int* height);

/**
 * @brief Converts a position in the oriented view that the window shows to the display's native position,
 *     the position with the display's rotation undone. LVGL rotates pointer positions from native to the
 *     rotated view itself, so pointers must report native positions.
 * @param[in,out] x the horizontal position
 * @param[in,out] y the vertical position
 */
void sdl_display_view_to_native(int32_t* x, int32_t* y);

/**
 * @brief Re-presents the already-drawn frame at the renderer's current scale. Call this when the
 *     window is resized: the window size change alone doesn't make SDL re-blit the last frame at
 *     the new scale until something calls SDL_RenderPresent() again, and LVGL won't do that on
 *     its own since nothing it's tracking actually changed. No-op if the display hasn't drawn its
 *     first frame yet.
 */
void sdl_display_present_now(void);

/**
 * @brief Presents the frame again when the brightness or the on/off state changed since the last present.
 * Only call it from the main thread.
 */
void sdl_display_present_if_requested(void);

/**
 * @brief Sets the simulated backlight: the frame is shown mixed with black, but never fully black.
 * Turning the display off is what makes it black. The main thread presents the change.
 * @param[in] brightness 0 is the dimmest and 255 is the frame as it was drawn
 */
void sdl_display_set_brightness(uint8_t brightness);

/** Tells the display which device is its backlight, or NULL when it has none */
void sdl_display_set_backlight_device(struct Device* device);

/** @return the simulated backlight brightness */
uint8_t sdl_display_get_brightness(void);

#ifdef __cplusplus
}
#endif
