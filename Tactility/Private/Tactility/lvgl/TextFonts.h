#pragma once

namespace tt::lvgl {

/**
 * Loads the small, default and large text fonts and registers them with lvgl_set_text_font().
 * A font is rasterized from the system TTF when it's not cached in the data path yet.
 * Must be called before LVGL starts.
 */
void initTextFonts();

}
