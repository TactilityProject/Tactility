// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LVGL_STATUSBAR_ICON_LIMIT 16
#define LVGL_STATUSBAR_IMAGE_MAX_LENGTH 63

/**
 * @brief Set the function that opens the system menu, called when the statusbar's icons are clicked or the statusbar is swiped down.
 * @param[in] callback the function, or NULL to do nothing. It gets the horizontal position of the swipe in display
 * pixels, or -1 when the icons were clicked.
 */
void lvgl_statusbar_set_menu_callback(void (*callback)(int32_t swipe_x));

/** @brief Set whether the time is shown in the 24-hour format. The default is true. */
void lvgl_statusbar_set_time_format_24h(bool format_24h);

/**
 * @brief Create a statusbar widget.
 * @warning Needs to be called with the LVGL lock.
 */
lv_obj_t* lvgl_statusbar_create(lv_obj_t* parent);

/**
 * How important an icon is. It doesn't change the order of the icons. When they don't fit, the icons with the lowest
 * priority are hidden first, and among icons of one priority the one that was added last.
 */
enum LvglStatusbarIconPriority {
    LVGL_STATUSBAR_ICON_PRIORITY_LOW,
    LVGL_STATUSBAR_ICON_PRIORITY_REGULAR,
    LVGL_STATUSBAR_ICON_PRIORITY_HIGH,
    LVGL_STATUSBAR_ICON_PRIORITY_CRITICAL,
};

/**
 * @brief Add an icon to all statusbars, with the regular priority. Does not need the LVGL lock.
 * @param[in] image the icon (e.g. a statusbar icon font symbol) or NULL for none, at most LVGL_STATUSBAR_IMAGE_MAX_LENGTH bytes
 * @param[in] visible whether the icon is shown
 * @return the icon id, or -1 when LVGL_STATUSBAR_ICON_LIMIT icons were added already. The other icon functions ignore id -1.
 */
int8_t lvgl_statusbar_icon_add(const char* image, bool visible);

/** @brief Same as lvgl_statusbar_icon_add(), with a priority that decides which icons stay when there is no room for all of them. */
int8_t lvgl_statusbar_icon_add_with_priority(const char* image, bool visible, enum LvglStatusbarIconPriority priority);

/** @brief Remove an icon. Does not need the LVGL lock. Does nothing for id -1. */
void lvgl_statusbar_icon_remove(int8_t id);

/**
 * @brief Update an icon's image. Does not need the LVGL lock. Does nothing for id -1.
 * @param[in] image the icon or NULL for none, at most LVGL_STATUSBAR_IMAGE_MAX_LENGTH bytes
 */
void lvgl_statusbar_icon_set_image(int8_t id, const char* image);

/** @brief Update an icon's visibility. Does not need the LVGL lock. Does nothing for id -1. */
void lvgl_statusbar_icon_set_visibility(int8_t id, bool visible);

/** @return the height of the statusbar in the split layout */
int32_t lvgl_statusbar_get_height(void);

/**
 * @brief Switch between the split layout (a bar on top) and the side layout (a vertical strip on the left).
 * Circular displays always use the split layout. The caller places the statusbar in its parent.
 * @warning Needs to be called with the LVGL lock.
 */
void lvgl_statusbar_set_side(lv_obj_t* statusbar, bool side);

/**
 * @brief Places the strip of the side layout on the right of the screen instead of the left. This decides which key leaves the strip.
 * The caller places the statusbar in its parent.
 * @warning Needs to be called with the LVGL lock.
 */
void lvgl_statusbar_set_side_right(lv_obj_t* statusbar, bool right);

/**
 * @brief Adds padding above and below the content of the strip in the side layout, e.g. to keep it clear of the rounded corners of a display.
 * @param[in] statusbar the statusbar
 * @param[in] top the extra padding at the top of the strip in pixels
 * @param[in] bottom the extra padding at the bottom of the strip in pixels
 * @warning Needs to be called with the LVGL lock.
 */
void lvgl_statusbar_set_side_insets(lv_obj_t* statusbar, int32_t top, int32_t bottom);

/**
 * @return the container for the action buttons of apps in the side layout, or NULL on circular displays. It is hidden in the split layout.
 * @warning Needs to be called with the LVGL lock.
 */
lv_obj_t* lvgl_statusbar_get_action_container(lv_obj_t* statusbar);

#ifdef __cplusplus
}
#endif
