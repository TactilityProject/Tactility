// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The look of an icon button, as styled by the theme */
enum LvglIconButtonVariant {
    /** No container, the icon uses the theme's icon colour */
    LVGL_ICON_BUTTON_STANDARD,
    /** Filled with the primary colour, for the most important action */
    LVGL_ICON_BUTTON_FILLED,
    /** Filled with a softer secondary colour, for other prominent actions */
    LVGL_ICON_BUTTON_TONAL
};

/**
 * @brief Creates a button that shows an icon (or short text) without a filled background.
 * It behaves like a regular button, but the theme styles it as an icon button.
 * @param[in] parent the parent object for the new button
 * @return the created button
 */
lv_obj_t* lvgl_icon_button_create(lv_obj_t* parent);

/**
 * @brief Creates an icon button with the specified look.
 * @param[in] parent the parent object for the new button
 * @param[in] variant the look of the button
 * @return the created button
 */
lv_obj_t* lvgl_icon_button_create_variant(lv_obj_t* parent, enum LvglIconButtonVariant variant);

#ifdef __cplusplus
}
#endif
