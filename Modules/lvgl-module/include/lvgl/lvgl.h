// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>
#include <stdbool.h>

// Official LVGL library header
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Affects LVGL widget style */
enum UiDensity {
    /** Ideal for very small non-touch screen devices (e.g. Waveshare S3 LCD 1.3") */
    LVGL_UI_DENSITY_COMPACT,
    /** Nothing was changed in the LVGL UI/UX */
    LVGL_UI_DENSITY_DEFAULT
};

/**
 * @brief Locks the LVGL mutex.
 *
 * This should be called before any LVGL API calls from threads other than the LVGL task.
 * It is a recursive mutex.
 * @retval true when a lock was acquired, false otherwise
 */
void lvgl_lock(void);

/**
 * @brief Tries to lock the LVGL mutex with a timeout.
 *
 * @param timeout Timeout in ticks
 * @return true if the lock was acquired, false otherwise.
 */
bool lvgl_try_lock(uint32_t timeout);

/**
 * @brief Unlocks the LVGL mutex.
 */
void lvgl_unlock(void);

/**
 * @brief Runs a function with the LVGL lock held, on the calling task.
 *
 * The function must not read files. The LVGL task can have its stack in external RAM.
 * @param[in] function the function to run
 * @param[in] argument passed to @a function
 */
void lvgl_run_on_task(void (*function)(void* argument), void* argument);

/**
 * @brief Checks if the LVGL module is currently running.
 *
 * @return true if running, false otherwise.
 */
bool lvgl_is_running(void);

/**
 * @brief Gets the UI density of the theme settings.
 * @deprecated Use lvgl_theme_get_size() and lvgl_theme_get_spacing() from <lvgl/theme.h>
 * @return the UI density
 */
__attribute__((deprecated("Use lvgl_theme_get_size() and lvgl_theme_get_spacing()")))
enum UiDensity lvgl_get_ui_density(void);

#ifdef __cplusplus
}
#endif
