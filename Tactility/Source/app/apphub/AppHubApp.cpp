#include <Tactility/Mutex.h>
#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/app/apphubdetails/AppHubDetailsApp.h>

#include <app/event.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <app_hub/app_hub.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/wifi.h>
#include <tactility/log.h>

#include <lvgl/fonts.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/icons/shared.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/chip.h>
#include <lvgl/widgets/list.h>
#include <lvgl/widgets/toolbar.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <format>
#include <string>
#include <vector>

#include <tactility/std/string.hpp>
#include <tactility/std/vector.hpp>

namespace tt::app::apphub {

constexpr auto* TAG = "AppHub";

extern const ::AppManifest manifest;

namespace {

enum class Filter {
    All,
    Installed,
    Updates
};

constexpr auto FILTER_LABELS = ::std::to_array<const char*>({ "All", "Installed", "Updates" });

struct Context {
    uint32_t appInstanceId;

    lv_obj_t* contentWrapper = nullptr;
    lv_obj_t* refreshButton = nullptr;
    lv_obj_t* updateAllButton = nullptr;
    lv_obj_t* updateAllSpinner = nullptr;
    lv_obj_t* filterChips = nullptr;
    // Survives across a bury/resurface cycle
    Filter filter = Filter::All;
    /** The app ids of the list's buttons, by index */
    tt::std::vector<tt::std::string> appIds;
    /** Read from the catalog file on this app's own thread (see loadApps()), as the widget code can't do file operations */
    tt::std::vector<AppHubApp> apps;
    Mutex mutex;

    // Survives across a bury/resurface cycle (e.g. opening AppHubDetailsApp and returning),
    int32_t scrollY = 0;
    // Set by createWidgets(), consumed by the first showApps() after resurfacing. The refresh
    // itself runs async (see requestRefresh() below), so when showApps() first populates the
    // list, contentWrapper is still an empty spinner; scrollY must come from here instead of a
    // live read off it.
    bool restoreScrollOnNextShow = false;
    // Only the first createWidgets() call triggers a network refresh. The rest uses the cached catalog.
    bool needsInitialRefresh = true;

    TaskEventGroup* eventGroup = nullptr;
    uint32_t refreshRequestedBit = 0;
    ::std::atomic<bool> refreshRequested {false};
    /** Set when the window resurfaces: the installed apps can change without an App Hub event, e.g. through the Files app */
    ::std::atomic<bool> reloadRequested {false};

    AppHubEventSubscription appHubSub {};
    /** The id of the queued catalog refresh, 0 when none is pending */
    ::std::atomic<uint32_t> refreshOperationId {0};
    /** The id of the running update of all apps, 0 when none is running */
    uint32_t updateAllOperationId = 0;
    /** The ids of the apps that failed to update during updateAllOperationId */
    tt::std::vector<tt::std::string> failedUpdates;
};


void showApps(Context* ctx);
void refresh(Context* ctx);

/** Reads the apps from App Hub for showApps(). Doesn't touch widgets, so it runs without the LVGL lock. */
void loadApps(Context* ctx) {
    tt::std::vector<AppHubApp> apps;
    app_hub_search(nullptr, [](const AppHubApp* app, void* context) {
        static_cast<tt::std::vector<AppHubApp>*>(context)->push_back(*app);
        return true;
    }, &apps);
    ctx->mutex.lock();
    ctx->apps = ::std::move(apps);
    ctx->mutex.unlock();
}

bool matchesFilter(const AppHubApp& app, Filter filter) {
    switch (filter) {
        case Filter::All:
            return true;
        case Filter::Installed:
            return app.installed;
        case Filter::Updates:
            return app.update_available;
    }
    return true;
}

const char* getEmptyFilterText(Filter filter) {
    return (filter == Filter::Updates) ? "No updates available" : "No installed apps";
}

void updateFilterChips(Context* ctx) {
    const uint32_t count = lv_obj_get_child_count(ctx->filterChips);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_set_state(lv_obj_get_child(ctx->filterChips, static_cast<int32_t>(i)), LV_STATE_CHECKED, i == static_cast<uint32_t>(ctx->filter));
    }
}

/**
 * Shows the spinner while App Hub is busy, and otherwise the update all button when there are updates.
 * Requires the LVGL lock.
 */
void updateToolbarActions(Context* ctx, bool hasUpdates) {
    if (ctx->updateAllSpinner == nullptr) {
        return;
    }
    const bool busy = app_hub_is_busy();
    lv_obj_set_hidden(ctx->updateAllSpinner, !busy);
    lv_obj_set_hidden(ctx->updateAllButton, busy || !hasUpdates);
}

void onUpdateAllPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    const error_t error = app_hub_update_all(nullptr);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to queue the update of all apps: %s", error_to_string(error));
        return;
    }
    // Shows the spinner
    showApps(ctx);
}

void updateUpdatesChipText(Context* ctx, size_t updateCount) {
    auto* chip = lv_obj_get_child(ctx->filterChips, static_cast<int32_t>(Filter::Updates));
    const auto text = ::std::format("{} {}", updateCount, updateCount == 1 ? "update" : "updates");
    lv_label_set_text(lv_obj_get_child(chip, 0), text.c_str());
}

void onFilterPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* chip = lv_event_get_target_obj(event);
    const auto filter = static_cast<Filter>(lv_obj_get_index(chip));
    if (filter == ctx->filter) {
        return;
    }
    ctx->filter = filter;
    updateFilterChips(ctx);
    // A different filter shows a different list, which starts at the top
    ctx->scrollY = 0;
    ctx->restoreScrollOnNextShow = true;
    showApps(ctx);
}

/** The filter pills wrap onto the next row when they don't fit */
lv_obj_t* createFilterChips(lv_obj_t* parent, Context* ctx) {
    auto* chips = lv_obj_create(parent);
    lv_obj_set_size(chips, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(chips, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(chips, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(chips, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(chips, 0, LV_STATE_DEFAULT);
    // The chips' margins leave room for their focus rings and space them apart
    lv_obj_set_style_pad_column(chips, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(chips, 0, LV_STATE_DEFAULT);
    lv_obj_set_scrollable(chips, false);

    for (const auto* label : FILTER_LABELS) {
        auto* chip = lvgl_chip_create(chips);
        lv_obj_add_event_cb(chip, onFilterPressed, LV_EVENT_SHORT_CLICKED, ctx);
        lv_label_set_text(lv_label_create(chip), label);
    }
    lvgl_grid_navigation_add(chips);
    return chips;
}

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
    if (index < ctx->appIds.size()) {
        apphubdetails::start(ctx->appIds[index].c_str());
    }
    ctx->mutex.unlock();
}

void requestRefresh(Context* ctx) {
    ctx->refreshRequested = true;
    task_event_group_signal(ctx->eventGroup, ctx->refreshRequestedBit);
}

void requestReload(Context* ctx) {
    ctx->reloadRequested = true;
    task_event_group_signal(ctx->eventGroup, ctx->refreshRequestedBit);
}

void onRefreshPressed(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    requestRefresh(ctx);
}

lv_obj_t* createCard(lv_obj_t* parent) {
    auto* card = lvgl_card_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    return card;
}

/** Replaces the content with a centered card that holds the status text */
void showStatusLabel(Context* ctx, const char* text) {
    lv_obj_clean(ctx->contentWrapper);

    auto* card = createCard(ctx->contentWrapper);
    lv_obj_set_style_text_align(card, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    auto* label = lv_label_create(card);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(label, text);
}

void showRefreshFailedError(Context* ctx, const char* message) {
    showStatusLabel(ctx, message);

    lv_obj_set_hidden(ctx->refreshButton, false);
    updateToolbarActions(ctx, false);
}

void showNoInternet(Context* ctx) {
    showRefreshFailedError(ctx, "No Internet Connection");
}

bool isInContentWrapper(Context* ctx, lv_obj_t* obj) {
    for (auto* parent = lv_obj_get_parent(obj); parent != nullptr; parent = lv_obj_get_parent(parent)) {
        if (parent == ctx->contentWrapper) {
            return true;
        }
    }
    return false;
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
    ctx->mutex.lock();
    // Rebuilding deletes the focused entry button, so the same app is focused again afterwards
    tt::std::string focused_app_id;
    bool focused_by_key = false;
    auto* group = lv_group_get_default();
    auto* focused = group != nullptr ? lv_group_get_focused(group) : nullptr;
    if (focused != nullptr && isInContentWrapper(ctx, focused)) {
        const auto index = reinterpret_cast<intptr_t>(lv_obj_get_user_data(focused));
        if (index < ctx->appIds.size()) {
            focused_app_id = ctx->appIds[index];
            focused_by_key = lv_obj_has_state(focused, LV_STATE_FOCUS_KEY);
        }
    }
    lv_obj_clean(ctx->contentWrapper);

    tt::std::vector<AppHubApp> apps = ctx->apps;
    const bool hasApps = !apps.empty();
    updateUpdatesChipText(ctx, ::std::ranges::count_if(apps, [](const AppHubApp& app) { return app.update_available; }));
    updateToolbarActions(ctx, ::std::ranges::any_of(apps, [](const AppHubApp& app) { return app.update_available; }));
    ::std::erase_if(apps, [ctx](const AppHubApp& app) {
        return !matchesFilter(app, ctx->filter);
    });
    ctx->appIds.clear();

    if (!apps.empty()) {
        auto* card = createCard(ctx->contentWrapper);
        lv_obj_set_style_pad_all(card, 0, LV_STATE_DEFAULT);
        // The list items' pressed and focused backgrounds follow the card's rounded corners
        lv_obj_set_style_clip_corner(card, true, LV_STATE_DEFAULT);

        // The card provides the background
        auto* list = lvgl_list_create(card);
        lv_obj_set_style_pad_all(list, 0, LV_STATE_DEFAULT);
        lv_obj_set_size(list, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(list, 0, LV_STATE_DEFAULT);
        lv_obj_t* focus_target = nullptr;
        for (int i = 0; i < apps.size(); i++) {
            const auto& app = apps[i];
            const char* icon = app.operation_pending ? LVGL_ICON_SHARED_HOURGLASS : (app.installed ? LV_SYMBOL_OK : nullptr);
            auto* entry_button = lvgl_list_add_button(list, icon, app.name);
            if (app.operation_pending) {
                lv_obj_set_style_text_font(lv_obj_get_child(entry_button, 0), lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
            }
            auto int_as_voidptr = reinterpret_cast<void*>(i);
            lv_obj_set_user_data(entry_button, int_as_voidptr);
            lv_obj_add_event_cb(entry_button, onAppPressed, LV_EVENT_SHORT_CLICKED, ctx);
            ctx->appIds.emplace_back(app.id);
            if (app.id == focused_app_id) {
                focus_target = entry_button;
            }
        }

        lv_obj_scroll_to_y(ctx->contentWrapper, scrollY, LV_ANIM_OFF);
        if (focus_target != nullptr) {
            lv_group_focus_obj(focus_target);
            if (focused_by_key) {
                lv_obj_add_state(focus_target, LV_STATE_FOCUS_KEY);
            }
        }
    } else if (hasApps) {
        showStatusLabel(ctx, getEmptyFilterText(ctx->filter));
    } else if (ctx->refreshOperationId == 0) {
        showRefreshFailedError(ctx, "Failed to load content");
    } else {
        showStatusLabel(ctx, "Loading...");
    }
    ctx->mutex.unlock();
}

// Runs on appMain()'s own task (triggered via requestRefresh()), never directly from the LVGL task.
void refresh(Context* ctx) {
    // Buried (e.g. AppHubDetailsApp is open): destroyWidgets() already released these. A refresh
    // request queued just before burying could still land here, so re-check rather than assume
    // requestRefresh() and refresh() always run against a live window.
    if (ctx->refreshOperationId != 0) {
        return;
    }

    lvgl_lock();
    if (ctx->contentWrapper == nullptr) {
        lvgl_unlock();
        return;
    }
    lv_obj_set_hidden(ctx->refreshButton, true);
    lvgl_unlock();

    WifiStationState station_state = WIFI_STATION_STATE_DISCONNECTED;
    Device* wifi_device = nullptr;
    if (device_get_first_by_type(&WIFI_TYPE, &wifi_device) == ERROR_NONE) {
        wifi_get_station_state(wifi_device, &station_state);
        device_put(wifi_device);
    }

    if (station_state != WIFI_STATION_STATE_CONNECTED) {
        lvgl_lock();
        showNoInternet(ctx);
        lvgl_unlock();
        return;
    }

    uint32_t operation_id = 0;
    if (app_hub_refresh(&operation_id) != ERROR_NONE) {
        LOG_E(TAG, "Failed to queue the catalog refresh");
        lvgl_lock();
        showRefreshFailedError(ctx, "Cannot reach server");
        lvgl_unlock();
        return;
    }
    ctx->refreshOperationId = operation_id;

    // Shows the cached catalog while the new one downloads
    loadApps(ctx);
    lvgl_lock();
    if (ctx->contentWrapper != nullptr) {
        showApps(ctx);
    }
    lvgl_unlock();
}

// Runs on appMain()'s own task, after the window resurfaced
void reload(Context* ctx) {
    loadApps(ctx);
    lvgl_lock();
    if (ctx->contentWrapper != nullptr) {
        showApps(ctx);
    }
    lvgl_unlock();
}

void onOperationEvent(Context* ctx, const AppHubEvent& event) {
    if (event.type == APP_HUB_EVENT_REFRESHING) {
        // The list doesn't change yet, only the toolbar shows that App Hub is busy
        lvgl_lock();
        if (ctx->updateAllButton != nullptr) {
            updateToolbarActions(ctx, !lv_obj_is_hidden(ctx->updateAllButton));
        }
        lvgl_unlock();
        return;
    }

    const bool isRefresh = event.operation_id == ctx->refreshOperationId;
    if (isRefresh) {
        ctx->refreshOperationId = 0;
        ctx->needsInitialRefresh = event.result != ERROR_NONE;
        if (event.result != ERROR_NONE) {
            LOG_E(TAG, "Catalog refresh failed: %s", error_to_string(event.result));
        }
    }

    loadApps(ctx);
    lvgl_lock();
    if (ctx->contentWrapper == nullptr) {
        // Buried (e.g. AppHubDetailsApp or a dialog is open): createWidgets() rebuilds the list on resurface
    } else if (isRefresh && event.result != ERROR_NONE) {
        showRefreshFailedError(ctx, "Cannot reach server");
    } else {
        showApps(ctx);
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
    lv_obj_set_hidden(ctx->refreshButton, true);
    ctx->updateAllButton = lvgl_toolbar_add_image_button_action(toolbar, LV_SYMBOL_DOWNLOAD, onUpdateAllPressed, ctx);
    lv_obj_set_hidden(ctx->updateAllButton, true);
    ctx->updateAllSpinner = lvgl_toolbar_add_spinner_action(toolbar);
    lv_obj_set_hidden(ctx->updateAllSpinner, true);

    ctx->filterChips = createFilterChips(parent, ctx);
    updateFilterChips(ctx);

    ctx->contentWrapper = lv_obj_create(parent);
    lv_obj_set_style_border_width(ctx->contentWrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_width(ctx->contentWrapper, LV_PCT(100));
    lv_obj_set_flex_grow(ctx->contentWrapper, 1);

    ctx->restoreScrollOnNextShow = true;
    if (ctx->needsInitialRefresh && ctx->refreshOperationId == 0) {
        requestRefresh(ctx);
    } else {
        // Resurfacing (e.g. returning from AppHubDetailsApp): redisplay the cached catalog
        // instead of hitting the network again. window_manager calls createWidgets() with the
        // LVGL lock already held, so this can touch widgets directly.
        showApps(ctx);
        // The cache can be stale, so the app's task reloads it and shows the list again
        requestReload(ctx);
    }
}

void destroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->scrollY = lv_obj_get_scroll_y(ctx->contentWrapper);
    ctx->contentWrapper = nullptr;
    ctx->refreshButton = nullptr;
    ctx->updateAllButton = nullptr;
    ctx->updateAllSpinner = nullptr;
    ctx->filterChips = nullptr;
}

/** Tracks the update of all apps, and shows which apps failed when it's done */
void onUpdateAllEvent(Context* ctx, const AppHubEvent& event) {
    if (event.type == APP_HUB_EVENT_UPDATING) {
        ctx->updateAllOperationId = event.operation_id;
        ctx->failedUpdates.clear();
    } else if (event.type == APP_HUB_EVENT_INSTALLED && event.operation_id == ctx->updateAllOperationId && event.result != ERROR_NONE) {
        ctx->failedUpdates.emplace_back(event.app_id);
    } else if (event.type == APP_HUB_EVENT_UPDATED && event.operation_id == ctx->updateAllOperationId) {
        ctx->updateAllOperationId = 0;
        if (!ctx->failedUpdates.empty()) {
            tt::std::string names;
            for (const auto& id : ctx->failedUpdates) {
                AppHubApp app {};
                if (!names.empty()) {
                    names += ", ";
                }
                names += (app_hub_get_app(id.c_str(), &app) == ERROR_NONE) ? app.name : id.c_str();
            }
            const auto count = ctx->failedUpdates.size();
            alertdialog::start(ctx->appInstanceId, "Update failed", ::std::format("{} {} failed to update: {}", count, count == 1 ? "app" : "apps", names));
            ctx->failedUpdates.clear();
        }
    }
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
    check(app_hub_event_subscribe(&ctx.appHubSub, &event_group) == ERROR_NONE);

    loadApps(&ctx);
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

        AppHubEvent app_hub_event {};
        while (app_hub_event_poll(&ctx.appHubSub, &app_hub_event) == ERROR_NONE) {
            onUpdateAllEvent(&ctx, app_hub_event);
            onOperationEvent(&ctx, app_hub_event);
        }

        if (!shouldClose && ctx.reloadRequested.exchange(false)) {
            reload(&ctx);
        }
        if (!shouldClose && ctx.refreshRequested.exchange(false)) {
            refresh(&ctx);
        }
    }

    window_manager_remove(window);
    check(app_hub_event_unsubscribe(&ctx.appHubSub) == ERROR_NONE);
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
        .desired_memory_capability = 0,
    },
};

} // namespace tt::app::apphub
