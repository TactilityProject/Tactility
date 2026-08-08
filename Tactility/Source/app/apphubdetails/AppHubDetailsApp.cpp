#include "../../../../Modules/app-module/private/app/private/app_ledger.h"
#include "app/metadata.h"


#include <Tactility/Paths.h>
#include <Tactility/StringUtils.h>
#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/app/apphub/AppHub.h>
#include <Tactility/app/apphub/AppHubEntry.h>
#include <Tactility/file/File.h>
#include <Tactility/network/Http.h>

#include <app/event.h>
#include <app/install.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

#include <lvgl/lvgl.h>
#include <lvgl/widgets/toolbar.h>

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
};


void updateViews(Context* ctx);

uint32_t showConfirmDialog(Context* ctx, const char* action) {
    const auto message = std::format("{} {}?", action, ctx->entry.appName);
    return alertdialog::start(ctx->appInstanceId, CONFIRM_TEXT, message, std::vector<std::string> { CONFIRM_TEXT, CANCEL_TEXT });
}

void onBackPressed(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    // Async, non-blocking - must NOT call app_manager_stop() directly here: that bound-waits
    // (thread_join) for this app's own thread to finish, which needs the LVGL lock
    // (window_manager_remove()) - but this callback runs ON the LVGL task, which would
    // deadlock against itself.
    AppEvent closeEvent { .type = APP_EVENT_CLOSE, .timestamp = 0, .result = {} };
    app_event_emit(ctx->appInstanceId, &closeEvent);
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

void doInstall(Context* ctx) {
    auto url = apphub::getDownloadUrl(ctx->entry.file);
    auto file_name = file::getLastPathSegment(ctx->entry.file);
    auto temp_file_path = std::format("{}/{}", getTempPath(), file_name);
    network::http::download(
        url,
        apphub::CERTIFICATE_PATH,
        temp_file_path,
        [ctx, temp_file_path] {
            app_install(temp_file_path.c_str());

            if (!file::deleteFile(temp_file_path)) {
                LOG_W(TAG, "Failed to remove %s", temp_file_path.c_str());
            } else {
                LOG_I(TAG, "Deleted temporary file %s", temp_file_path.c_str());
            }

            lvgl_lock();
            updateViews(ctx);
            lvgl_unlock();
        },
        [ctx, temp_file_path](const char* errorMessage) {
            LOG_E(TAG, "Download failed: %s", errorMessage);
            alertdialog::start(ctx->appInstanceId, "Error", "Failed to install app");

            if (file::isFile(temp_file_path) && !file::deleteFile(temp_file_path.c_str())) {
                LOG_W(TAG, "Failed to remove %s", temp_file_path.c_str());
            }
        }
    );
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
    const auto manifest = app_manager_find_manifest(app_id);
    ctx->spinner = lvgl_toolbar_add_spinner_action(ctx->toolbar);
    lv_obj_add_flag(ctx->spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ctx->updateLabel, LV_OBJ_FLAG_HIDDEN);

    char install_path[128];
    if (app_get_install_path(app_id, install_path, sizeof(install_path)) != ERROR_NONE) {
        LOG_E(TAG, "Install path not found for %s", app_id);
        return;
    }

    std::string metadata_path = std::string(install_path) + "/manifest.properties";
    AppMetadata metadata;
    if (app_metadata_parse(metadata_path.c_str(), &metadata) != ERROR_NONE) {
        LOG_E(TAG, "Failed to parse metadata at %s", metadata_path.c_str());
        return;
    }

    if (manifest != nullptr) {
        if (metadata.app_version_code < ctx->entry.appVersionCode) {
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

int32_t appMain(uint32_t appInstanceId, int argc, char* argv[]) {
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

    AppEventSubscription sub {};
    sub.app_instance_id = appInstanceId;
    app_event_subscribe(&sub);

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);

    bool shouldClose = false;
    while (!shouldClose) {
        AppEvent event {};
        if (app_event_await(&sub, &event, portMAX_DELAY) != ERROR_NONE) {
            break;
        }
        switch (event.type) {
            case APP_EVENT_CLOSE:
                app_manager_finish(appInstanceId);
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
    }

    window_manager_remove(window);
    app_event_unsubscribe(&sub);

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
    app_manager_start_for_result(manifest.id, /*parent_instance_id=*/0, static_cast<int>(argv.size()), argv.data(), &instanceId);
}

extern const ::AppManifest manifest = {
    .id = "AppHubDetails",
    .name = "App Details",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
};

} // namespace
