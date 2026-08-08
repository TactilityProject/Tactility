#include <Tactility/Mutex.h>
#include <Tactility/Paths.h>
#include <Tactility/app/apphub/AppHub.h>
#include <Tactility/app/apphub/AppHubEntry.h>
#include <Tactility/app/apphubdetails/AppHubDetailsApp.h>
#include <Tactility/file/File.h>
#include <Tactility/network/Http.h>
#include <Tactility/service/wifi/Wifi.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/log.h>

#include <lvgl/lvgl.h>
#include <lvgl/widgets/spinner.h>
#include <lvgl/widgets/toolbar.h>

#include <algorithm>
#include <format>

namespace tt::app::apphub {

constexpr auto* TAG = "AppHub";

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;

    lv_obj_t* contentWrapper = nullptr;
    lv_obj_t* refreshButton = nullptr;
    std::string cachedAppsJsonFile = std::format("{}/app_hub.json", getTempPath());
    std::vector<AppHubEntry> entries;
    Mutex mutex;
};


void showApps(Context* ctx);
void refresh(Context* ctx);

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    // Async, non-blocking - must NOT call app_manager_stop() directly here: that bound-waits
    // (thread_join) for this app's own thread to finish, which needs the LVGL lock
    // (window_manager_remove()) - but this callback runs ON the LVGL task, which would
    // deadlock against itself.
    AppEvent closeEvent { .type = APP_EVENT_CLOSE, .timestamp = 0, .result = {} };
    app_event_emit(ctx->appInstanceId, &closeEvent);
}

void onAppPressed(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    auto* widget = lv_event_get_target_obj(e);
    const auto* user_data = lv_obj_get_user_data(widget);
    const intptr_t index = reinterpret_cast<intptr_t>(user_data);
    ctx->mutex.lock();
    if (index < ctx->entries.size()) {
        apphubdetails::start(ctx->entries[index]);
    }
    ctx->mutex.unlock();
}

void onRefreshPressed(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    refresh(ctx);
}

void showRefreshFailedError(Context* ctx, const char* message) {
    lv_obj_clean(ctx->contentWrapper);

    auto* label = lv_label_create(ctx->contentWrapper);
    lv_label_set_text(label, message);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_remove_flag(ctx->refreshButton, LV_OBJ_FLAG_HIDDEN);
}

void showNoInternet(Context* ctx) {
    showRefreshFailedError(ctx, "No Internet Connection");
}

void showApps(Context* ctx) {
    lv_obj_clean(ctx->contentWrapper);
    ctx->mutex.lock();
    if (parseJson(ctx->cachedAppsJsonFile, ctx->entries)) {
        std::ranges::sort(ctx->entries, [](auto left, auto right) {
            return left.appName < right.appName;
        });

        auto* list = lv_list_create(ctx->contentWrapper);
        lv_obj_set_style_pad_all(list, 0, LV_STATE_DEFAULT);
        lv_obj_set_size(list, LV_PCT(100), LV_SIZE_CONTENT);
        for (int i = 0; i < ctx->entries.size(); i++) {
            auto& entry = ctx->entries[i];
            LOG_I(TAG, "Adding %s", entry.appName.c_str());
            const char* icon = app_manager_find_manifest(entry.appId.c_str()) != nullptr ? LV_SYMBOL_OK : nullptr;
            auto* entry_button = lv_list_add_button(list, icon, entry.appName.c_str());
            auto int_as_voidptr = reinterpret_cast<void*>(i);
            lv_obj_set_user_data(entry_button, int_as_voidptr);
            lv_obj_add_event_cb(entry_button, onAppPressed, LV_EVENT_SHORT_CLICKED, ctx);
        }
    } else {
        showRefreshFailedError(ctx, "Failed to load content");
    }
    ctx->mutex.unlock();
}

void refresh(Context* ctx) {
    lv_obj_clean(ctx->contentWrapper);
    auto* spinner = lvgl_spinner_create(ctx->contentWrapper);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_flag(ctx->refreshButton, LV_OBJ_FLAG_HIDDEN);

    if (service::wifi::getRadioState() != service::wifi::RadioState::ConnectionActive) {
        showNoInternet(ctx);
        return;
    }

    if (file::isFile(ctx->cachedAppsJsonFile)) {
        showApps(ctx);
    }

    // These callbacks run on a background network thread and reach back into this app's
    // widgets via the captured ctx pointer - same convention as AppHubDetailsApp.cpp's
    // download callback for the sibling "install/update" flow.
    network::http::download(
        getAppsJsonUrl(),
        CERTIFICATE_PATH,
        ctx->cachedAppsJsonFile,
        [ctx] {
            LOG_I(TAG, "Request success");
            lvgl_lock();
            showApps(ctx);
            lvgl_unlock();
        },
        [ctx](const char* error) {
            LOG_E(TAG, "Request failed: %s", error);
            lvgl_lock();
            showRefreshFailedError(ctx, "Cannot reach server");
            lvgl_unlock();
        }
    );
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "App Hub");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);
    ctx->refreshButton = lvgl_toolbar_add_image_button_action(toolbar, LV_SYMBOL_REFRESH, onRefreshPressed, ctx);
    lv_obj_add_flag(ctx->refreshButton, LV_OBJ_FLAG_HIDDEN);

    ctx->contentWrapper = lv_obj_create(parent);
    lv_obj_set_width(ctx->contentWrapper, LV_PCT(100));
    lv_obj_set_flex_grow(ctx->contentWrapper, 1);
    lv_obj_set_style_pad_all(ctx->contentWrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_ver(ctx->contentWrapper, 0, LV_STATE_DEFAULT);

    refresh(ctx);
}

int32_t appMain(uint32_t appInstanceId, int argc, char* argv[]) {
    Context ctx;
    ctx.appInstanceId = appInstanceId;

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
            default:
                break;
        }
    }

    window_manager_remove(window);
    app_event_unsubscribe(&sub);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "AppHub",
    .name = "App Hub",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) }
};

} // namespace
