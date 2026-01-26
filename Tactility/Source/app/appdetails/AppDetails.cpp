#include "Tactility/lvgl/LvglSync.h"

#include <Tactility/app/App.h>
#include <Tactility/app/AppContext.h>
#include <Tactility/app/AppManifest.h>
#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/Check.h>
#include <Tactility/lvgl/Style.h>
#include <Tactility/lvgl/Toolbar.h>

#include <lvgl.h>
#include <format>
#include <Tactility/StringUtils.h>
#include <Tactility/file/File.h>

namespace tt::app::appdetails {

extern const AppManifest manifest;

void start(const std::string& appId) {
    auto bundle = std::make_shared<Bundle>();
    bundle->putString("appId", appId);
    app::start(manifest.appId, bundle);
}

class AppDetailsApp : public App {

    std::shared_ptr<AppManifest> manifest;

    static void onPressUninstall(lv_event_t* event) {
        auto* self = static_cast<AppDetailsApp*>(lv_event_get_user_data(event));
        std::vector<std::string> choices = {
            "Yes",
            "No"
        };
        alertdialog::start("Confirmation", std::format("Uninstall {}?", self->manifest->appName), choices);
    }

public:

    void onCreate(AppContext& app) override {
        const auto parameters = app.getParameters();
        check(parameters != nullptr, "Parameters missing");
        auto app_id = parameters->getString("appId");
        manifest = findAppManifestById(app_id);
        assert(manifest != nullptr);
    }

    void onShow(AppContext& app, lv_obj_t* parent) override {
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

        auto title = std::format("{} details", manifest->appName);
        lvgl::toolbar_create(parent, title);

        auto* wrapper = lv_obj_create(parent);
        lv_obj_set_width(wrapper, LV_PCT(100));
        lv_obj_set_flex_grow(wrapper, 1);
        lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_border_width(wrapper, 0, LV_STATE_DEFAULT);
        lvgl::obj_set_style_bg_invisible(wrapper);

        auto identifier = std::format("Identifier: {}", manifest->appId);
        auto* identifier_label = lv_label_create(wrapper);
        lv_label_set_text(identifier_label, identifier.c_str());

        auto* location_label = lv_label_create(wrapper);
        std::string location;
        if (manifest->appLocation.isInternal()) {
            location = "internal";
        } else {
            if (!string::getPathParent(manifest->appLocation.getPath(), location)) {
                location = "external";
            }
        }
        std::string location_label_text = std::format("Location: {}", location);
        lv_label_set_text(location_label, location_label_text.c_str());

        if (manifest->appLocation.isExternal()) {
            auto* uninstall_button = lv_button_create(wrapper);
            lv_obj_set_width(uninstall_button, LV_PCT(100));
            lv_obj_add_event_cb(uninstall_button, onPressUninstall, LV_EVENT_SHORT_CLICKED, this);
            auto* uninstall_label = lv_label_create(uninstall_button);
            lv_obj_align(uninstall_label, LV_ALIGN_CENTER, 0, 0);
            lv_label_set_text(uninstall_label, "Uninstall");
        }
    }

    void onResult(AppContext& appContext, LaunchId launchId, Result result, std::unique_ptr<Bundle> bundle) override {
        if (result != Result::Ok || bundle == nullptr) {
            return;
        }

        if (alertdialog::getResultIndex(*bundle) != 0) { // 0 = Yes
            return;
        }

        uninstall(manifest->appId);

        // Stop app
        stop();
    }
};

extern const AppManifest manifest = {
    .appId = "AppDetails",
    .appName = "App Details",
    .appCategory = Category::System,
    .appFlags = AppManifest::Flags::Hidden,
    .createApp = create<AppDetailsApp>
};

} // namespace

