#include <Tactility/app/apphub/AppHub.h>
#include <Tactility/app/apphub/AppHubEntry.h>
#include <Tactility/app/apphubdetails/AppHubDetailsApp.h>
#include <Tactility/file/File.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/lvgl/Spinner.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/Logger.h>
#include <Tactility/network/Http.h>
#include <Tactility/Paths.h>
#include <Tactility/service/loader/Loader.h>
#include <Tactility/service/wifi/Wifi.h>

#include <lvgl.h>
#include <algorithm>
#include <format>

namespace tt::app::apphub {

static const auto LOGGER = Logger("AppHub");

extern const AppManifest manifest;

class AppHubApp final : public App {

    lv_obj_t* contentWrapper = nullptr;
    lv_obj_t* refreshButton = nullptr;
    std::string cachedAppsJsonFile = std::format("{}/app_hub.json", getTempPath());
    std::unique_ptr<Thread> thread;
    std::vector<AppHubEntry> entries;
    Mutex mutex;

    static std::shared_ptr<AppHubApp> _Nullable findAppInstance() {
        auto app_context = getCurrentAppContext();
        if (app_context->getManifest().appId != manifest.appId) {
            return nullptr;
        }
        return std::static_pointer_cast<AppHubApp>(app_context->getApp());
    }

    static void onAppPressed(lv_event_t* e) {
        const auto* self = static_cast<AppHubApp*>(lv_event_get_user_data(e));
        auto* widget = lv_event_get_target_obj(e);
        const auto* user_data = lv_obj_get_user_data(widget);
        const intptr_t index = reinterpret_cast<intptr_t>(user_data);
        self->mutex.lock();
        if (index < self->entries.size()) {
            apphubdetails::start(self->entries[index]);
        }
        self->mutex.unlock();
    }

    static void onRefreshPressed(lv_event_t* e) {
        auto* self = static_cast<AppHubApp*>(lv_event_get_user_data(e));
        self->refresh();
    }

    void onRefreshSuccess() {
        LOGGER.info("Request success");
        auto lock = lvgl::getSyncLock()->asScopedLock();
        lock.lock();

        showApps();
    }

    void onRefreshError(const char* error) {
        LOGGER.error("Request failed: {}", error);
        auto lock = lvgl::getSyncLock()->asScopedLock();
        lock.lock();

        showRefreshFailedError("Cannot reach server");
    }

    static void createAppWidget(const std::shared_ptr<AppManifest>& manifest, lv_obj_t* list) {
        lv_obj_t* btn = lv_list_add_button(list, nullptr, manifest->appName.c_str());
        lv_obj_add_event_cb(btn, &onAppPressed, LV_EVENT_SHORT_CLICKED, manifest.get());
    }

    void showRefreshFailedError(const char* message) {
        lv_obj_clean(contentWrapper);

        auto* label = lv_label_create(contentWrapper);
        lv_label_set_text(label, message);
        lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

        lv_obj_remove_flag(refreshButton, LV_OBJ_FLAG_HIDDEN);
    }

    void showNoInternet() {
        showRefreshFailedError("No Internet Connection");
    }

    void showTimeNotSynced() {
        showRefreshFailedError("Time is not synced yet.\nIt's required to establish a secure connection.");
    }

    void showApps() {
        lv_obj_clean(contentWrapper);
        mutex.lock();
        if (parseJson(cachedAppsJsonFile, entries)) {
            std::ranges::sort(entries, [](auto left, auto right) {
                return left.appName < right.appName;
            });

            auto* list = lv_list_create(contentWrapper);
            lv_obj_set_style_pad_all(list, 0, LV_STATE_DEFAULT);
            lv_obj_set_size(list, LV_PCT(100), LV_SIZE_CONTENT);
            for (int i = 0; i < entries.size(); i++) {
                auto& entry = entries[i];
                LOGGER.info("Adding {}", entry.appName.c_str());
                const char* icon = findAppManifestById(entry.appId) != nullptr ? LV_SYMBOL_OK : nullptr;
                auto* entry_button = lv_list_add_button(list, icon, entry.appName.c_str());
                auto int_as_voidptr = reinterpret_cast<void*>(i);
                lv_obj_set_user_data(entry_button, int_as_voidptr);
                lv_obj_add_event_cb(entry_button, onAppPressed, LV_EVENT_SHORT_CLICKED, this);
            }
        } else {
            showRefreshFailedError("Failed to load content");
        }
        mutex.unlock();
    }

    void refresh() {
        lv_obj_clean(contentWrapper);
        auto* spinner = lvgl::spinner_create(contentWrapper);
        lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 0);

        lv_obj_add_flag(refreshButton, LV_OBJ_FLAG_HIDDEN);

        if (service::wifi::getRadioState() != service::wifi::RadioState::ConnectionActive) {
            showNoInternet();
            return;
        }

        if (file::isFile(cachedAppsJsonFile)) {
            showApps();
        }

        network::http::download(
            getAppsJsonUrl(),
            CERTIFICATE_PATH,
            cachedAppsJsonFile,
            [] {
                auto app = findAppInstance();
                if (app != nullptr) {
                    app->onRefreshSuccess();
                }
            },
            [](const char* error) {
                auto app = findAppInstance();
                if (app != nullptr) {
                    app->onRefreshError(error);
                }
            }
        );
    }

public:

    void onShow(AppContext& app, lv_obj_t* parent) override {
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

        auto* toolbar = lvgl::toolbar_create(parent, app);
        refreshButton = lvgl::toolbar_add_image_button_action(toolbar, LV_SYMBOL_REFRESH, onRefreshPressed, this);
        lv_obj_add_flag(refreshButton, LV_OBJ_FLAG_HIDDEN);

        contentWrapper = lv_obj_create(parent);
        lv_obj_set_width(contentWrapper, LV_PCT(100));
        lv_obj_set_flex_grow(contentWrapper, 1);
        lv_obj_set_style_pad_all(contentWrapper, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_pad_ver(contentWrapper, 0, LV_STATE_DEFAULT);

        refresh();
    }
};

extern const AppManifest manifest = {
    .appId = "AppHub",
    .appName = "App Hub",
    .appCategory = Category::System,
    .createApp = create<AppHubApp>,
};

} // namespace
