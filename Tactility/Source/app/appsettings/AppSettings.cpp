#include <tactility/lvgl_fonts.h>
#include <tactility/lvgl_icon_shared.h>

#include <Tactility/app/AppRegistration.h>
#include <Tactility/app/appdetails/AppDetails.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/service/loader/Loader.h>

#include <lvgl.h>
#include <algorithm>

namespace tt::app::appsettings {

class AppSettingsApp final : public App {

    static void onAppPressed(lv_event_t* e) {
        const auto* manifest = static_cast<const AppManifest*>(lv_event_get_user_data(e));
        appdetails::start(manifest->appId);
    }

    static void createAppWidget(const std::shared_ptr<AppManifest>& manifest, lv_obj_t* list) {
        const void* icon = !manifest->appIcon.empty() ? manifest->appIcon.c_str() : LVGL_ICON_SHARED_TOOLBAR;
        lv_obj_t* btn = lv_list_add_button(list, icon, manifest->appName.c_str());
        lv_obj_t* image = lv_obj_get_child(btn, 0);
        lv_obj_set_style_text_font(image, lvgl_get_shared_icon_font(), LV_PART_MAIN);
        lv_obj_add_event_cb(btn, &onAppPressed, LV_EVENT_SHORT_CLICKED, manifest.get());
    }

public:

    void onShow(AppContext& app, lv_obj_t* parent) override {
        auto* toolbar = lvgl::toolbar_create(parent, "Installed Apps");
        lv_obj_align(toolbar, LV_ALIGN_TOP_MID, 0, 0);

        lv_obj_t* list = lv_list_create(parent);
        lv_obj_set_width(list, LV_PCT(100));
        lv_obj_align_to(list, toolbar, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

        auto toolbar_height = lv_obj_get_height(toolbar);
        auto parent_content_height = lv_obj_get_content_height(parent);
        lv_obj_set_height(list, parent_content_height - toolbar_height);

        auto manifests = getAppManifests();
        std::ranges::sort(manifests, SortAppManifestByName);

        size_t app_count = 0;
        for (const auto& manifest: manifests) {
            if (manifest->appLocation.isExternal()) {
                app_count++;
                createAppWidget(manifest, list);
            }
        }

        if (app_count == 0) {
            auto* no_apps_label = lv_label_create(parent);
            lv_label_set_text(no_apps_label, "No apps installed");
            lv_obj_align(no_apps_label, LV_ALIGN_CENTER, 0, 0);
        }
    }
};

extern const AppManifest manifest = {
    .appId = "AppSettings",
    .appName = "Apps",
    .appIcon = LVGL_ICON_SHARED_APPS,
    .appCategory = Category::Settings,
    .createApp = create<AppSettingsApp>,
};

} // namespace
