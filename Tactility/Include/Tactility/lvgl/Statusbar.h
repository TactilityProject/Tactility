#pragma once

#include <lvgl.h>

#include <string>

namespace tt::lvgl {

constexpr auto STATUSBAR_ICON_LIMIT = 8;

/** Create a statusbar widget. Needs to be called with LVGL lock. */
lv_obj_t* statusbar_create(lv_obj_t* parent);

/** Add an icon to the statusbar. Does not need to be called with LVGL lock. */
int8_t statusbar_icon_add(const std::string& image, bool visible);

/** Add an icon to the statusbar. Does not need to be called with LVGL lock. */
int8_t statusbar_icon_add();

/** Remove an icon from the statusbar. Does not need to be called with LVGL lock. */
void statusbar_icon_remove(int8_t id);

/** Update an icon's image from the statusbar. Does not need to be called with LVGL lock. */
void statusbar_icon_set_image(int8_t id, const std::string& image);

/** Update the visibility for an icon on the statusbar. Does not need to be called with LVGL lock. */
void statusbar_icon_set_visibility(int8_t id, bool visible);

/**
 * Sets the app that starts when the icon is clicked. Does not need to be called with LVGL lock.
 * @param[in] id the icon id
 * @param[in] appId the app id, or an empty string to make the icon not clickable
 */
void statusbar_icon_set_app(int8_t id, const std::string& appId);

int statusbar_get_height();

} // namespace
