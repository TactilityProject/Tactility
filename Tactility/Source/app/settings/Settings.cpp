#include <Tactility/app/AppRegistration.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/service/loader/Loader.h>

#include <Tactility/Assets.h>
#include <tactility/check.h>
#include <lvgl.h>

#include <algorithm>

namespace tt::app::settings {

static void onAppPressed(lv_event_t* e) {
    const auto* manifest = static_cast<const AppManifest*>(lv_event_get_user_data(e));
    start(manifest->appId);
}

static void createWidget(const std::shared_ptr<AppManifest>& manifest, void* parent) {
    check(parent);
    auto* list = (lv_obj_t*)parent;
    const void* icon = !manifest->appIcon.empty() ? manifest->appIcon.c_str() : TT_ASSETS_APP_ICON_FALLBACK;
    auto* btn = lv_list_add_button(list, icon, manifest->appName.c_str());
    lv_obj_add_event_cb(btn, &onAppPressed, LV_EVENT_SHORT_CLICKED, (void*)manifest.get());
}

class SettingsApp final : public App {

    void onShow(AppContext& app, lv_obj_t* parent) override {
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

        lvgl::toolbar_create(parent, app);

        auto* list = lv_list_create(parent);
        lv_obj_set_width(list, LV_PCT(100));
        lv_obj_set_flex_grow(list, 1);

        auto manifests = getAppManifests();
        std::ranges::sort(manifests, SortAppManifestByName);
        for (const auto& manifest: manifests) {
            if (manifest->appCategory == Category::Settings) {
                createWidget(manifest, list);
            }
        }
    }
};

extern const AppManifest manifest = {
    .appId = "Settings",
    .appName = "Settings",
    .appIcon = TT_ASSETS_APP_ICON_SETTINGS,
    .appCategory = Category::System,
    .appFlags = AppManifest::Flags::Hidden,
    .createApp = create<SettingsApp>
};

} // namespace
