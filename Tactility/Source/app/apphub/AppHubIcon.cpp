#include <Tactility/app/apphub/AppHubIcon.h>

#include <app_hub/app_hub.h>

#include <generated_assets.h>

#include <lvgl/svg.h>
#include <lvgl/theme.h>

#include <tactility/paths.h>

#include <initializer_list>

namespace tt::app::apphub {

// Icons are square, the corner radius is the one of a 96 px icon with a 22 px radius
constexpr uint32_t ICON_SIZE = 96;
constexpr uint32_t ICON_CORNER_RADIUS = 22;

lv_draw_buf_t* loadIcon(const char* appId, uint32_t size, bool* recolor) {
    const bool preferMono = lvgl_theme_is_mono();
    // Only the preferred variant is downloaded, the other one is a fallback
    for (const bool mono : { preferMono, !preferMono }) {
        char path[FILE_MAX_PATH_STRING_LENGTH];
        if (app_hub_get_icon(appId, mono, path, sizeof(path)) != ERROR_NONE) {
            continue;
        }
        auto* image = lvgl_svg_rasterize_file_variants(mono ? nullptr : path, mono ? path : nullptr, preferMono, size, recolor);
        if (image == nullptr) {
            continue;
        }
        if (!*recolor) {
            lvgl_svg_round_corners(image, size * ICON_CORNER_RADIUS / ICON_SIZE);
        }
        return image;
    }
    return nullptr;
}

lv_draw_buf_t* loadDefaultIcon(uint32_t size) {
    return lvgl_svg_rasterize_data(reinterpret_cast<const char*>(asset_app_icon_default_mono_svg), asset_app_icon_default_mono_svg_SIZE, size);
}

void setIcon(lv_obj_t* image, const lv_draw_buf_t* icon, bool recolor) {
    lv_image_set_src(image, icon);
    lv_obj_set_style_image_recolor(image, lv_theme_get_color_primary(image), LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(image, recolor ? LV_OPA_COVER : LV_OPA_TRANSP, LV_STATE_DEFAULT);
}

}
