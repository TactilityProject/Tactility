#include <Tactility/DeprecatedPaths.h>
#include <Tactility/Mutex.h>
#include <Tactility/app/apphub/AppHub.h>
#include <Tactility/app/apphub/AppHubEntry.h>
#include <Tactility/app/apphubdetails/AppHubDetailsApp.h>
#include <Tactility/file/File.h>
#include <Tactility/service/wifi/Wifi.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <http/download.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/log.h>

#include <lvgl/lvgl.h>
#include <lvgl/widgets/spinner.h>
#include <lvgl/widgets/toolbar.h>

#include <algorithm>
#include <atomic>
#include <format>
#include <string_view>

namespace tt::app::apphub {

constexpr auto* TAG = "AppHub";

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;

    lv_obj_t* contentWrapper = nullptr;
    lv_obj_t* refreshButton = nullptr;
    std::string cachedAppsJsonFile = std::format("{}/app_hub.json", getTempPath());
    AppHubEntryList entries;
    Mutex mutex;

    // Survives across a bury/resurface cycle (e.g. opening AppHubDetailsApp and returning),
    int32_t scrollY = 0;
    // Set by createWidgets(), consumed by the first showApps() after resurfacing. The refresh
    // itself runs async (see requestRefresh() below), so when showApps() first populates the
    // list, contentWrapper is still an empty spinner; scrollY must come from here instead of a
    // live read off it.
    bool restoreScrollOnNextShow = false;
    // Only the first createWidgets() call triggers a network refresh. The rest uses the cached file.
    bool needsInitialRefresh = true;

    TaskEventGroup* eventGroup = nullptr;
    uint32_t refreshRequestedBit = 0;
    std::atomic<bool> refreshRequested {false};

    HttpDownloadSubscription downloadSub {};
    bool downloadInProgress = false;
};


void showApps(Context* ctx);
void refresh(Context* ctx);

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
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

void requestRefresh(Context* ctx) {
    ctx->refreshRequested = true;
    task_event_group_signal(ctx->eventGroup, ctx->refreshRequestedBit);
}

void onRefreshPressed(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    requestRefresh(ctx);
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
    // Refresh rebuilds the list from scratch (cached copy, then again once the network fetch
    // lands), which would otherwise reset the user's scroll position each time.
    int32_t scrollY;
    if (ctx->restoreScrollOnNextShow) {
        scrollY = ctx->scrollY;
        ctx->restoreScrollOnNextShow = false;
    } else {
        scrollY = lv_obj_get_scroll_y(ctx->contentWrapper);
    }
    lv_obj_clean(ctx->contentWrapper);
    ctx->mutex.lock();
    if (parseJson(ctx->cachedAppsJsonFile, ctx->entries)) {
        // An empty targetPlatforms list means the entry runs everywhere; otherwise it must name
        // this build's own target to be installable here. The simulator isn't a real MCU target,
        // so it has nothing to match against and skips this filter entirely.
        std::erase_if(ctx->entries, [](const AppHubEntry& entry) {
#ifdef ESP_PLATFORM
            return !entry.targetPlatforms.empty() &&
                std::ranges::find(entry.targetPlatforms, std::string_view(CONFIG_IDF_TARGET)) == entry.targetPlatforms.end();
#else
            (void)entry;
            return false;
#endif
        });

        std::ranges::sort(ctx->entries, [](auto left, auto right) {
            return left.appName < right.appName;
        });

        auto* list = lv_list_create(ctx->contentWrapper);
        lv_obj_set_style_pad_all(list, 0, LV_STATE_DEFAULT);
        lv_obj_set_size(list, LV_PCT(100), LV_SIZE_CONTENT);
        for (int i = 0; i < ctx->entries.size(); i++) {
            auto& entry = ctx->entries[i];
            LOG_I(TAG, "Adding %s", entry.appName.c_str());
            AppManifest manifest;
            const char* icon = app_manager_find_manifest(entry.appId.c_str(), &manifest) == ERROR_NONE ? LV_SYMBOL_OK : nullptr;
            auto* entry_button = lv_list_add_button(list, icon, entry.appName.c_str());
            auto int_as_voidptr = reinterpret_cast<void*>(i);
            lv_obj_set_user_data(entry_button, int_as_voidptr);
            lv_obj_add_event_cb(entry_button, onAppPressed, LV_EVENT_SHORT_CLICKED, ctx);
        }

        lv_obj_scroll_to_y(ctx->contentWrapper, scrollY, LV_ANIM_OFF);
    } else {
        showRefreshFailedError(ctx, "Failed to load content");
    }
    ctx->mutex.unlock();
}

// Runs on appMain()'s own task (triggered via requestRefresh()), never directly from the LVGL task.
void refresh(Context* ctx) {
    // Buried (e.g. AppHubDetailsApp is open): destroyWidgets() already released these. A refresh
    // request queued just before burying could still land here, so re-check rather than assume
    // requestRefresh() and refresh() always run against a live window.
    if (ctx->downloadInProgress || ctx->contentWrapper == nullptr) {
        return;
    }

    lvgl_lock();
    lv_obj_clean(ctx->contentWrapper);
    auto* spinner = lvgl_spinner_create(ctx->contentWrapper);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(ctx->refreshButton, LV_OBJ_FLAG_HIDDEN);
    lvgl_unlock();

    if (service::wifi::getRadioState() != service::wifi::RadioState::ConnectionActive) {
        lvgl_lock();
        showNoInternet(ctx);
        lvgl_unlock();
        return;
    }

    if (file::isFile(ctx->cachedAppsJsonFile)) {
        lvgl_lock();
        showApps(ctx);
        lvgl_unlock();
    }

    if (http_download_subscribe(&ctx->downloadSub, ctx->eventGroup) != ERROR_NONE) {
        LOG_E(TAG, "Failed to subscribe to download events");
        lvgl_lock();
        showRefreshFailedError(ctx, "Cannot reach server");
        lvgl_unlock();
        return;
    }

    auto url = getAppsJsonUrl();
    if (http_download_start(url.c_str(), CERTIFICATE_PATH, ctx->cachedAppsJsonFile.c_str(), &ctx->downloadSub) != ERROR_NONE) {
        LOG_E(TAG, "Failed to start download");
        http_download_unsubscribe(&ctx->downloadSub);
        lvgl_lock();
        showRefreshFailedError(ctx, "Cannot reach server");
        lvgl_unlock();
        return;
    }

    ctx->downloadInProgress = true;
}

// Called from appMain()'s loop once http_download_poll() reports the download's terminal event.
void onDownloadFinished(Context* ctx, const HttpDownloadEvent& event) {
    ctx->downloadInProgress = false;
    http_download_unsubscribe(&ctx->downloadSub);

    bool succeeded = event.type == HTTP_DOWNLOAD_EVENT_SUCCESS;
    ctx->needsInitialRefresh = !succeeded;
    if (succeeded) {
        LOG_I(TAG, "Request success (status %d)", event.status_code);
    } else {
        LOG_E(TAG, "Request failed (status %d): %s", event.status_code, event.error.message);
    }

    if (ctx->contentWrapper == nullptr) {
        // Buried (e.g. AppHubDetailsApp is open): destroyWidgets() already released the widgets
        // above. createWidgets() picks this up via needsInitialRefresh on resurface instead.
        return;
    }

    lvgl_lock();
    if (succeeded) {
        showApps(ctx);
    } else {
        showRefreshFailedError(ctx, "Cannot reach server");
    }
    lvgl_unlock();
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

    ctx->restoreScrollOnNextShow = true;
    if (ctx->needsInitialRefresh) {
        requestRefresh(ctx);
    } else {
        // Resurfacing (e.g. returning from AppHubDetailsApp): redisplay the cache already
        // loaded this session instead of hitting the network again. window_manager calls
        // createWidgets() with the LVGL lock already held, so this can touch widgets directly.
        showApps(ctx);
    }
}

void destroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->scrollY = lv_obj_get_scroll_y(ctx->contentWrapper);
    ctx->contentWrapper = nullptr;
    ctx->refreshButton = nullptr;
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    Context ctx;
    ctx.appInstanceId = appInstanceId;

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);
    ctx.eventGroup = &event_group;
    if (task_event_group_claim_bit(&event_group, &ctx.refreshRequestedBit) != ERROR_NONE) {
        LOG_W(TAG, "Failed to claim a refresh-requested bit; refresh button won't work");
    }

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    WindowId window = window_manager_create_ext(appInstanceId, createWidgets, destroyWidgets, &ctx);

    bool shouldClose = false;
    while (!shouldClose) {
        task_event_group_wait_any(&event_group, nullptr, portMAX_DELAY);

        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            switch (event.type) {
                case APP_EVENT_CLOSE:
                    shouldClose = true;
                    break;
                default:
                    break;
            }
        }

        if (ctx.downloadInProgress) {
            HttpDownloadEvent download_event {};
            if (http_download_poll(&ctx.downloadSub, &download_event) == ERROR_NONE) {
                onDownloadFinished(&ctx, download_event);
            }
        }

        if (!shouldClose && ctx.refreshRequested.exchange(false)) {
            refresh(&ctx);
        }
    }

    if (ctx.downloadInProgress) {
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

extern const ::AppManifest manifest = {
    .id = "tactility.apphub",
    .name = "App Hub",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {
        .depth = 10240,
        .desired_memory_capability = 0
    }
};

} // namespace tt::app::apphub
