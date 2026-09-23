// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/error.h>

#include <stdint.h>

struct Device;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Runs forever, pumping SDL input and executing display present jobs submitted via
 * sdl_bridge_present(). Must be called exactly once, from the real OS main thread: macOS requires
 * SDL/Cocoa window creation, event pumping and rendering to happen there, but FreeRTOS tasks
 * (including the lvgl task that owns display flush and indev polling) run on separate pthreads
 * spawned by the FreeRTOS POSIX port, not on that thread.
 */
void sdl_bridge_run_main_loop(void);

/**
 * @brief Hands a display flush off to the main thread and blocks until it has finished copying
 * the pixel data out (see sdl_display_execute_draw_bitmap() in sdl_display.cpp). Called from the
 * lvgl task. Must block: the caller's pixel buffer is single-buffered and gets reused as soon as
 * this returns.
 */
error_t sdl_bridge_present(struct Device* device, void* internal, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data);

/**
 * @brief Implemented in sdl_display.cpp: the actual SDL work behind a display flush (lazy window
 * init on first call, SDL_UpdateTexture, present). Only ever called from sdl_bridge_run_main_loop()
 * on the main thread.
 */
error_t sdl_display_execute_draw_bitmap(struct Device* device, void* internal, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data);

#ifdef __cplusplus
}
#endif
