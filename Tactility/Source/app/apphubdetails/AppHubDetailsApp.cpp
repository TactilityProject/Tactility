#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/app/apphub/AppHub.h>
#include <Tactility/app/apphub/AppHubEntry.h>
#include <Tactility/app/AppRegistration.h>
#include <Tactility/file/File.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/Logger.h>
#include <Tactility/network/Http.h>
#include <Tactility/Paths.h>
#include <Tactility/service/loader/Loader.h>
#include <Tactility/StringUtils.h>

#include <lvgl.h>
#include <format>

namespace tt::app::apphubdetails {

static const auto LOGGER = Logger("AppHubDetails");

extern const AppManifest manifest;

static std::shared_ptr<Bundle> toBundle(const apphub::AppHubEntry& entry) {
    auto bundle = std::make_shared<Bundle>();
    bundle->putString("appId", entry.appId);
    bundle->putString("appVersionName", entry.appVersionName);
    bundle->putInt32("appVersionCode", entry.appVersionCode);
    bundle->putString("appName", entry.appName);
    bundle->putString("appDescription", entry.appDescription);
    bundle->putString("targetSdk", entry.targetSdk);
    bundle->putString("file", entry.file);
    bundle->putString("targetPlatforms", string::join(entry.targetPlatforms, ","));
    return bundle;
}

static bool fromBundle(const Bundle& bundle, apphub::AppHubEntry& entry) {
    std::string target_platforms_string;
    auto result = bundle.optString("appId", entry.appId) &&
        bundle.optString("appVersionName", entry.appVersionName) &&
        bundle.optInt32("appVersionCode", entry.appVersionCode) &&
        bundle.optString("appName", entry.appName) &&
        bundle.optString("appDescription", entry.appDescription) &&
        bundle.optString("targetSdk", entry.targetSdk) &&
        bundle.optString("file", entry.file) &&
        bundle.optString("targetPlatforms", target_platforms_string);
    entry.targetPlatforms = string::split(target_platforms_string, ",");
    return result;
}

class AppHubDetailsApp final : public App {

    static constexpr auto* CONFIRM_TEXT = "Confirm";
    static constexpr auto* CANCEL_TEXT = "Cancel";
    static constexpr auto CONFIRMATION_BUTTON_INDEX = 0;
    const std::vector<const char*> CONFIRM_CANCEL_LABELS = { CONFIRM_TEXT, CANCEL_TEXT };

    apphub::AppHubEntry entry;
    std::shared_ptr<AppManifest> entryManifest;
    lv_obj_t* toolbar = nullptr;
    lv_obj_t* spinner = nullptr;
    lv_obj_t* updateButton = nullptr;
    lv_obj_t* updateLabel = nullptr;
    LaunchId installLaunchId = -1;
    LaunchId uninstallLaunchId = -1;
    LaunchId updateLaunchId = -1;

    LaunchId showConfirmDialog(const char* action) {
        const auto message = std::format("{} {}?", action, entry.appName);
        return alertdialog::start(CONFIRM_TEXT, message, CONFIRM_CANCEL_LABELS);
    }

    static void onInstallPressed(lv_event_t* e) {
        auto* self = static_cast<AppHubDetailsApp*>(lv_event_get_user_data(e));
        self->installLaunchId = self->showConfirmDialog("Install");
    }

    static void onUninstallPressed(lv_event_t* e) {
        auto* self = static_cast<AppHubDetailsApp*>(lv_event_get_user_data(e));
        self->uninstallLaunchId = self->showConfirmDialog("Uninstall");
    }

    static void onUpdatePressed(lv_event_t* e) {
        auto* self = static_cast<AppHubDetailsApp*>(lv_event_get_user_data(e));
        self->updateLaunchId = self->showConfirmDialog("Update");
    }

    void uninstallApp() {
        LOGGER.info("Uninstall");

        lvgl::getSyncLock()->lock();
        lv_obj_remove_flag(spinner, LV_OBJ_FLAG_HIDDEN);
        lvgl::getSyncLock()->unlock();

        uninstall(entry.appId);

        lvgl::getSyncLock()->lock();
        updateViews();
        lvgl::getSyncLock()->unlock();
    }

    void doInstall() {
        auto url = apphub::getDownloadUrl(entry.file);
        auto file_name = file::getLastPathSegment(entry.file);
        auto temp_file_path = std::format("{}/{}", getTempPath(), file_name);
        network::http::download(
            url,
            apphub::CERTIFICATE_PATH,
            temp_file_path,
            [this, temp_file_path] {
                install(temp_file_path);

                if (!file::deleteFile(temp_file_path)) {
                    LOGGER.warn("Failed to remove {}", temp_file_path);
                } else {
                    LOGGER.info("Deleted temporary file {}", temp_file_path);
                }

                lvgl::getSyncLock()->lock();
                updateViews();
                lvgl::getSyncLock()->unlock();
            },
            [temp_file_path](const char* errorMessage) {
                LOGGER.error("Download failed: {}", errorMessage);
                alertdialog::start("Error", "Failed to install app");

                if (file::isFile(temp_file_path) && !file::deleteFile(temp_file_path.c_str())) {
                    LOGGER.warn("Failed to remove {}", temp_file_path);
                }
            }
        );
    }

    void installApp() {
        LOGGER.info("Install");

        lvgl::getSyncLock()->lock();
        lv_obj_remove_flag(spinner, LV_OBJ_FLAG_HIDDEN);
        lvgl::getSyncLock()->unlock();

        doInstall();
    }

    void updateApp() {
        LOGGER.info("Update");

        lvgl::getSyncLock()->lock();
        lv_obj_remove_flag(spinner, LV_OBJ_FLAG_HIDDEN);
        lvgl::getSyncLock()->unlock();

        LOGGER.info("Removing previous version");
        uninstall(entry.appId);
        LOGGER.info("Installing new version");
        doInstall();
    }

    void updateViews() {
        lvgl::toolbar_clear_actions(toolbar);
        const auto manifest = findAppManifestById(entry.appId);
        spinner = lvgl::toolbar_add_spinner_action(toolbar);
        lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(updateLabel, LV_OBJ_FLAG_HIDDEN);
        if (manifest != nullptr) {
            if (manifest->appVersionCode < entry.appVersionCode) {
                updateButton = lvgl::toolbar_add_image_button_action(toolbar, LV_SYMBOL_DOWNLOAD, onUpdatePressed, this);
                lv_obj_remove_flag(updateLabel, LV_OBJ_FLAG_HIDDEN);
            }
            lvgl::toolbar_add_image_button_action(toolbar, LV_SYMBOL_TRASH, onUninstallPressed, this);
        } else {
            lvgl::toolbar_add_image_button_action(toolbar, LV_SYMBOL_DOWNLOAD, onInstallPressed, this);
        }
    }

public:

    void onCreate(AppContext& appContext) override {
        auto parameters = appContext.getParameters();
        if (parameters == nullptr) {
            LOGGER.error("No parameters");
            stop();
            return;
        }

        if (!fromBundle(*parameters.get(), entry)) {
            LOGGER.error("Invalid parameters");
            stop();
        }
    }

    void onShow(AppContext& app, lv_obj_t* parent) override {
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

        toolbar = lvgl::toolbar_create(parent, entry.appName.c_str());
        auto* wrapper = lv_obj_create(parent);
        lv_obj_set_width(wrapper, LV_PCT(100));
        lv_obj_set_flex_grow(wrapper, 1);
        lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);

        updateLabel = lv_label_create(wrapper);
        lv_label_set_text(updateLabel, "Update available!");
        lv_obj_set_style_text_color(updateLabel, lv_color_make(0xff, 0xff, 00), LV_STATE_DEFAULT);

        auto* description_label = lv_label_create(wrapper);
        lv_obj_set_width(description_label, LV_PCT(100));
        lv_label_set_long_mode(description_label, LV_LABEL_LONG_MODE_WRAP);
        if (!entry.appDescription.empty()) {
            lv_label_set_text(description_label, entry.appDescription.c_str());
        } else {
            lv_label_set_text(description_label, "This app has no description yet.");
        }

        auto* version_label = lv_label_create(wrapper);
        lv_label_set_text_fmt(version_label, "Version %s", entry.appVersionName.c_str());

        updateViews();
    }

    void onResult(AppContext& appContext, LaunchId launchId, Result result, std::unique_ptr<Bundle> resultData) override {
        if (result != Result::Ok) {
            return;
        }

        if (alertdialog::getResultIndex(*resultData.get()) != CONFIRMATION_BUTTON_INDEX) {
            return;
        }

        if (launchId == installLaunchId) {
            installApp();
        } else if (launchId == uninstallLaunchId) {
            uninstallApp();
        } else if (launchId == updateLaunchId) {
            updateApp();
        }
    }
};

void start(const apphub::AppHubEntry& entry) {
    const auto bundle = toBundle(entry);
    app::start(manifest.appId, bundle);
}

extern const AppManifest manifest = {
    .appId = "AppHubDetails",
    .appName = "App Details",
    .appCategory = Category::System,
    .appFlags = AppManifest::Flags::Hidden,
    .createApp = create<AppHubDetailsApp>,
};

} // namespace
