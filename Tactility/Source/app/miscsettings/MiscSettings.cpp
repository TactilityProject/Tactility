#ifdef ESP_PLATFORM

#include <lvgl.h>

#include <tactility/lvgl_icon_shared.h>
#include <tactility/lvgl_module.h>

#include <Tactility/Tactility.h>
#include <Tactility/settings/DisplaySettings.h>
#include <Tactility/service/displayidle/DisplayIdleService.h>
#include <Tactility/lvgl/Toolbar.h>

namespace tt::app::miscsettings {

class MiscSettingsApp final : public App {

public:
    void onShow(AppContext& app, lv_obj_t* parent) override {
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

        lvgl::toolbar_create(parent, app);
    }
};

extern const AppManifest manifest = {
    .appId = "MiscSettings",
    .appName = "Miscellaneous",
    .appIcon = LVGL_ICON_SHARED_MORE_VERT,
    .appCategory = Category::Settings,
    .createApp = create<MiscSettingsApp>
};

}

#endif
