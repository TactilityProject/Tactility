#pragma once

#include <lvgl.h>

#include <cstdint>

namespace tt::app::apphub {

/**
 * Downloads the icon of a catalog app when it isn't cached yet, and rasterizes it.
 * Blocks until the download finishes, so it must not be called on the LVGL thread.
 * @param[in] appId the id of a catalog app
 * @param[in] size width and height in pixels
 * @param[out] recolor true for a mono icon, which is tinted with the theme's icon colour
 * @return the image, to be freed with lv_draw_buf_destroy(), or nullptr when the app has no icon
 */
lv_draw_buf_t* loadIcon(const char* appId, uint32_t size, bool* recolor);

/**
 * Rasterizes the icon that shows for an app that has no icon (yet): a single colour glyph without a background.
 * Set it with setIcon() and recolor true.
 * @return the image, to be freed with lv_draw_buf_destroy(), or nullptr when it can't be rasterized
 */
lv_draw_buf_t* loadDefaultIcon(uint32_t size);

/** Sets the image of a (possibly still empty) icon widget, tinted when it's a mono icon */
void setIcon(lv_obj_t* image, const lv_draw_buf_t* icon, bool recolor);

}
