#include <Tactility/DeprecatedPaths.h>
#include <Tactility/StringUtils.h>
#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/app/apphub/AppHub.h>
#include <Tactility/app/apphub/AppHubEntry.h>
#include <Tactility/file/File.h>

#include <app/event.h>
#include <app/install.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/package_manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <http/download.h>

#include <lvgl_window_manager/window_manager.h>

#include <lvgl/lvgl.h>
#include <lvgl/widgets/toolbar.h>

#include <tactility/check.h>
#include <tactility/log.h>

#include <atomic>
#include <cstdlib>
#include <format>

namespace tt::app::apphubdetails {

constexpr auto* TAG = "AppHubDetails";

extern const ::AppManifest manifest;

namespace {

constexpr auto* CONFIRM_TEXT = "Confirm";
constexpr auto* CANCEL_TEXT = "Cancel";
constexpr int32_t CONFIRMATION_BUTTON_INDEX = 0;

struct Context {
    uint32_t appInstanceId;
    apphub::AppHubEntry entry;

    lv_obj_t* toolbar = nullptr;
    lv_obj_t* spinner = nullptr;
    lv_obj_t* updateButton = nullptr;
    lv_obj_t* updateLabel = nullptr;

    // Set from the LVGL task (button press), read from this app's own thread (event loop) -
    // both directions cross threads, hence atomic.
    std::atomic<uint32_t> installDialogId = 0;
    std::atomic<uint32_t> uninstallDialogId = 0;
    std::atomic<uint32_t> updateDialogId = 0;

    // doInstall()/onDownloadFinished() and the poll for them both run on appMain()'s own task
    // (triggered via confirm-dialog APP_EVENT_RESULTs), so HttpDownloadSubscription's
    // subscribe/start/poll are naturally all on one consistent task already.
    TaskEventGroup* eventGroup = nullptr;
    HttpDownloadSubscription downloadSub {};
    bool downloadInProgress = false;
};


void updateViews(Context* ctx);

uint32_t showConfirmDialog(Context* ctx, const char* action) {
    const auto message = std::format("{} {}?", action, ctx->entry.appName);
    return alertdialog::start(ctx->appInstanceId, CONFIRM_TEXT, message, std::vector<std::string> { CONFIRM_TEXT, CANCEL_TEXT });
}

void onBackPressed(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    app_event_emit_close(ctx->appInstanceId);
}

void onInstallPressed(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    ctx->installDialogId = showConfirmDialog(ctx, "Install");
}

void onUninstallPressed(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    ctx->uninstallDialogId = showConfirmDialog(ctx, "Uninstall");
}

void onUpdatePressed(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    ctx->updateDialogId = showConfirmDialog(ctx, "Update");
}

void uninstallApp(Context* ctx) {
    LOG_I(TAG, "Uninstall");

    lvgl_lock();
    lv_obj_remove_flag(ctx->spinner, LV_OBJ_FLAG_HIDDEN);
    lvgl_unlock();

    app_uninstall(ctx->entry.appId.c_str());

    lvgl_lock();
    updateViews(ctx);
    lvgl_unlock();
}

// Path doInstall() downloads to and onDownloadFinished() installs from - deterministic from ctx->entry,
// which doesn't change once this app instance is running, so it's recomputed at each use instead of stored.
std::string getTempFilePath(Context* ctx) {
    auto file_name = file::getLastPathSegment(ctx->entry.file);
    return std::format("{}/{}", getTempPath(), file_name);
}

void doInstall(Context* ctx) {
    if (ctx->downloadInProgress) {
        return;
    }

    if (http_download_subscribe(&ctx->downloadSub, ctx->eventGroup) != ERROR_NONE) {
        LOG_E(TAG, "Failed to subscribe to download events");
        alertdialog::start(ctx->appInstanceId, "Error", "Failed to install app");
        return;
    }

    auto url = apphub::getDownloadUrl(ctx->entry.file);
    auto temp_file_path = getTempFilePath(ctx);
    if (http_download_start(url.c_str(), apphub::CERTIFICATE_PATH, temp_file_path.c_str(), &ctx->downloadSub) != ERROR_NONE) {
        LOG_E(TAG, "Failed to start download");
        http_download_unsubscribe(&ctx->downloadSub);
        alertdialog::start(ctx->appInstanceId, "Error", "Failed to install app");
        return;
    }

    ctx->downloadInProgress = true;
}

// Called from appMain()'s loop once http_download_poll() reports the download's terminal event.
void onDownloadFinished(Context* ctx, const HttpDownloadEvent& event) {
    ctx->downloadInProgress = false;
    http_download_unsubscribe(&ctx->downloadSub);

    auto temp_file_path = getTempFilePath(ctx);

    if (event.type == HTTP_DOWNLOAD_EVENT_SUCCESS) {
        error_t install_result = app_install(temp_file_path.c_str());
        if (install_result != ERROR_NONE) {
            LOG_E(TAG, "Install of %s failed", temp_file_path.c_str());
            alertdialog::start(ctx->appInstanceId, "Error", "Failed to install app");
        }

        if (!file::deleteFile(temp_file_path)) {
            LOG_W(TAG, "Failed to remove %s", temp_file_path.c_str());
        } else {
            LOG_I(TAG, "Deleted temporary file %s", temp_file_path.c_str());
        }

        lvgl_lock();
        updateViews(ctx);
        lvgl_unlock();
    } else {
        LOG_E(TAG, "Download failed (status %d): %s", event.status_code,
            event.type == HTTP_DOWNLOAD_EVENT_ERROR ? event.error.message : "Cancelled");
        alertdialog::start(ctx->appInstanceId, "Error", "Failed to install app");

        if (file::isFile(temp_file_path) && !file::deleteFile(temp_file_path.c_str())) {
            LOG_W(TAG, "Failed to remove %s", temp_file_path.c_str());
        }
    }
}

void installApp(Context* ctx) {
    LOG_I(TAG, "Install");

    lvgl_lock();
    lv_obj_remove_flag(ctx->spinner, LV_OBJ_FLAG_HIDDEN);
    lvgl_unlock();

    doInstall(ctx);
}

void updateApp(Context* ctx) {
    LOG_I(TAG, "Update");

    lvgl_lock();
    lv_obj_remove_flag(ctx->spinner, LV_OBJ_FLAG_HIDDEN);
    lvgl_unlock();

    LOG_I(TAG, "Removing previous version");
    app_uninstall(ctx->entry.appId.c_str());
    LOG_I(TAG, "Installing new version");
    doInstall(ctx);
}

void updateViews(Context* ctx) {
    lvgl_toolbar_clear_actions(ctx->toolbar);
    auto app_id = ctx->entry.appId.c_str();
    ctx->spinner = lvgl_toolbar_add_spinner_action(ctx->toolbar);
    lv_obj_add_flag(ctx->spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ctx->updateLabel, LV_OBJ_FLAG_HIDDEN);

    char install_path[128];
    bool is_installed = app_get_install_path(app_id, install_path, sizeof(install_path)) == ERROR_NONE
        && file::isFile(std::string(install_path) + "/manifest.properties");

    if (is_installed) {
        std::string metadata_path = std::string(install_path) + "/manifest.properties";
        PackageManifest package;
        if (app_package_manifest_parse(metadata_path.c_str(), &package, nullptr, 0) == ERROR_NONE
            && package.version_code < ctx->entry.appVersionCode) {
            ctx->updateButton = lvgl_toolbar_add_image_button_action(ctx->toolbar, LV_SYMBOL_DOWNLOAD, onUpdatePressed, ctx);
            lv_obj_remove_flag(ctx->updateLabel, LV_OBJ_FLAG_HIDDEN);
        }
        lvgl_toolbar_add_image_button_action(ctx->toolbar, LV_SYMBOL_TRASH, onUninstallPressed, ctx);
    } else {
        lvgl_toolbar_add_image_button_action(ctx->toolbar, LV_SYMBOL_DOWNLOAD, onInstallPressed, ctx);
    }
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    ctx->toolbar = lvgl_toolbar_create(parent, ctx->entry.appName.c_str());
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(ctx->toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);
    auto* wrapper = lv_obj_create(parent);
    lv_obj_set_width(wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(wrapper, 1);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);

    ctx->updateLabel = lv_label_create(wrapper);
    lv_label_set_text(ctx->updateLabel, "Update available!");
    lv_obj_set_style_text_color(ctx->updateLabel, lv_color_make(0xff, 0xff, 00), LV_STATE_DEFAULT);

    auto* description_label = lv_label_create(wrapper);
    lv_obj_set_width(description_label, LV_PCT(100));
    lv_label_set_long_mode(description_label, LV_LABEL_LONG_MODE_WRAP);
    if (!ctx->entry.appDescription.empty()) {
        std::string description = ctx->entry.appDescription;
        for (size_t pos = 0; (pos = description.find("\\n", pos)) != std::string::npos;) {
            description.replace(pos, 2, "\n");
        }
        lv_label_set_text(description_label, description.c_str());
    } else {
        lv_label_set_text(description_label, "This app has no description yet.");
    }

    auto* version_label = lv_label_create(wrapper);
    lv_label_set_text_fmt(version_label, "Version %s", ctx->entry.appVersionName.c_str());

    updateViews(ctx);
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    // argv layout: [0]=appId, [1]=appVersionName, [2]=appVersionCode, [3]=appName,
    // [4]=appDescription, [5]=targetSdk, [6]=file, [7..argc)=targetPlatforms.

    Context ctx {};
    ctx.appInstanceId = appInstanceId;
    if (argc >= 7) {
        ctx.entry.appId = argv[0];
        ctx.entry.appVersionName = argv[1];
        ctx.entry.appVersionCode = static_cast<int32_t>(strtol(argv[2], nullptr, 10));
        ctx.entry.appName = argv[3];
        ctx.entry.appDescription = argv[4];
        ctx.entry.targetSdk = argv[5];
        ctx.entry.file = argv[6];
        for (int i = 7; i < argc; i++) {
            ctx.entry.targetPlatforms.emplace_back(argv[i]);
        }
    }

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);
    ctx.eventGroup = &event_group;

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);

    bool shouldClose = false;
    while (!shouldClose) {
        task_event_group_wait_any(&event_group, nullptr, portMAX_DELAY);

        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            switch (event.type) {
                case APP_EVENT_CLOSE:
                    shouldClose = true;
                    break;
                case APP_EVENT_RESULT: {
                    bool confirmed = event.result.result == CONFIRMATION_BUTTON_INDEX;
                    if (event.result.launch_id == ctx.installDialogId && confirmed) {
                        installApp(&ctx);
                    } else if (event.result.launch_id == ctx.uninstallDialogId && confirmed) {
                        uninstallApp(&ctx);
                    } else if (event.result.launch_id == ctx.updateDialogId && confirmed) {
                        updateApp(&ctx);
                    }
                    app_manager_stop(event.result.launch_id);
                    break;
                }
                default:
                    break;
            }
            if (shouldClose) break;
        }

        if (ctx.downloadInProgress) {
            HttpDownloadEvent download_event {};
            if (http_download_poll(&ctx.downloadSub, &download_event) == ERROR_NONE) {
                onDownloadFinished(&ctx, download_event);
            }
        }
    }

    if (ctx.downloadInProgress) {
        // Safe to call immediately, even mid-download - no need to wait for the terminal event
        // first, so app close doesn't block on the network.
        http_download_cancel(&ctx.downloadSub);
        http_download_unsubscribe(&ctx.downloadSub);
        ctx.downloadInProgress = false;
    }

    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);

    return 0;
}

} // namespace

void start(const apphub::AppHubEntry& entry) {
    // Fire-and-forget (parent_instance_id 0): AppHub's own multi-app browsing list isn't
    // waiting on a result. targetPlatforms is variable-length, so it goes last in argv.
    std::string versionCode = std::to_string(entry.appVersionCode);
    std::vector<const char*> argv {
        entry.appId.c_str(),
        entry.appVersionName.c_str(),
        versionCode.c_str(),
        entry.appName.c_str(),
        entry.appDescription.c_str(),
        entry.targetSdk.c_str(),
        entry.file.c_str(),
    };
    for (const auto& platform: entry.targetPlatforms) {
        argv.push_back(platform.c_str());
    }
    uint32_t instanceId = 0;
    app_start_for_result(manifest.id, static_cast<int>(argv.size()), argv.data(), /*parent_instance_id=*/0, &instanceId);
}

extern const ::AppManifest manifest = {
    .id = "tactility.apphubdetails",
    .name = "App Details",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = {}
};

} // namespace
