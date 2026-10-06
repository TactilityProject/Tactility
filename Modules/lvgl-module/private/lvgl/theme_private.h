#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initializes the theme for a display. When the display needs the other theme than the active one,
 * the active theme is deinitialized first, so only one theme is allocated.
 * @param[in] display the display that the theme is for
 * @param[in] isMonoDisplay the display can only show monochrome or greyscale
 * @param[in] supportsAnimations the display can show animations (e.g. not e-paper)
 * @return the theme
 */
lv_theme_t* lvgl_theme_init_for_display(lv_display_t* display, bool isMonoDisplay, bool supportsAnimations);

/** Deinitializes the active theme. LVGL frees its memory when it stops, which includes the theme. */
void lvgl_theme_deinit(void);

#ifdef __cplusplus
}
#endif
