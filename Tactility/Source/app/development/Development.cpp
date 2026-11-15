#define L10N_APP "development"
#include <Tactility/i18n/AppDomain.h>
#include <Tactility/Tactility.h>
#include <Tactility/app/development/Fingerprint.h>
#include <Tactility/Timer.h>
#include <Tactility/lvgl/Style.h>

#include <development/server.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/concurrent/mutex.h>
#include <tactility/device.h>
#include <tactility/drivers/wifi.h>
#include <tactility/log.h>

#include <lvgl.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/toolbar.h>

#include <atomic>
#include <cstring>
#include <format>
#include <string>
#include <vector>

namespace tt::app::development {

constexpr auto* TAG = "Development";

extern const ::AppManifest manifest;

namespace {

constexpr TickType_t TRUSTED_CLIENTS_REFRESH_INTERVAL = pdMS_TO_TICKS(1000);

struct TrustedClient {
    std::string fingerprint;
    std::string name;

    bool operator==(const TrustedClient&) const = default;
};

struct Context {
    uint32_t appInstanceId;

    lv_obj_t* enableSwitch = nullptr;
    lv_obj_t* statusLabel = nullptr;
    lv_obj_t* trustedCard = nullptr;
    // The server is enabled on the app task, as that can generate a certificate
    TaskEventGroup* eventGroup = nullptr;
    uint32_t enableBit = 0;
    std::atomic<bool> enableRequested = false;
    // File IO for trusted clients happens on the app task
    uint32_t removeBit = 0;
    ::Mutex removalMutex {};
    std::vector<std::string> pendingRemovals;
    // Guarded by the LVGL lock, so createWidgets() can rebuild the list from it
    std::vector<TrustedClient> trustedClients;
    std::unique_ptr<Timer> timer;
};


void updateViewState(Context* ctx);

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void onEnableSwitchChanged(lv_event_t* event) {
    auto* widget = static_cast<lv_obj_t*>(lv_event_get_target(event));
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    ctx->enableRequested = lv_obj_has_state(widget, LV_STATE_CHECKED);
    task_event_group_signal(ctx->eventGroup, ctx->enableBit);
}

void updateViewState(Context* ctx) {
    WifiStationState station_state = WIFI_STATION_STATE_DISCONNECTED;
    char ip[16] = {};
    Device* wifi_device = nullptr;
    if (device_get_first_by_type(&WIFI_TYPE, &wifi_device) == ERROR_NONE) {
        wifi_get_station_state(wifi_device, &station_state);
        wifi_station_get_ipv4_address(wifi_device, ip);
        device_put(wifi_device);
    }

    if (!development_server_is_enabled()) {
        lv_label_set_text(ctx->statusLabel, tr("Service disabled"));
    } else if (station_state != WIFI_STATION_STATE_CONNECTED) {
        lv_label_set_text(ctx->statusLabel, tr("Waiting for connection..."));
    } else { // enabled and connected to wifi
        if (ip[0] == '\0') {
            lv_label_set_text(ctx->statusLabel, tr("Waiting for IP..."));
        } else {
            char fingerprint[DEVELOPMENT_SERVER_FINGERPRINT_LENGTH + 1] = {};
            development_server_get_fingerprint(fingerprint, sizeof(fingerprint));
            const std::string status = i18n::format(tr("Available at {}\nFingerprint: {}"), { ip, formatShortFingerprint(fingerprint) });
            lv_label_set_text(ctx->statusLabel, status.c_str());
        }
    }
}

void onRemoveTrustedClientPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* button = static_cast<lv_obj_t*>(lv_event_get_target(event));
    const auto index = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(button));
    if (index >= ctx->trustedClients.size()) {
        return;
    }
    mutex_lock(&ctx->removalMutex);
    ctx->pendingRemovals.push_back(ctx->trustedClients[index].fingerprint);
    mutex_unlock(&ctx->removalMutex);
    task_event_group_signal(ctx->eventGroup, ctx->removeBit);
}

void rebuildTrustedClients(Context* ctx) {
    lv_obj_clean(ctx->trustedCard);
    if (ctx->trustedClients.empty()) {
        lv_obj_t* none_label = lv_label_create(ctx->trustedCard);
        lv_label_set_text(none_label, tr("None"));
        return;
    }
    for (size_t index = 0; index < ctx->trustedClients.size(); index++) {
        const TrustedClient& client = ctx->trustedClients[index];

        lv_obj_t* row = lv_obj_create(ctx->trustedCard);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scrollable(row, false);
        lvgl::obj_set_style_bg_invisible(row);
        lv_obj_set_style_border_width(row, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(row, 0, LV_STATE_DEFAULT);

        lv_obj_t* label = lv_label_create(row);
        const std::string text = i18n::format("{}\n{}", { client.name.empty() ? std::string(tr("Unknown")) : std::string(client.name), formatShortFingerprint(client.fingerprint) });
        lv_label_set_text(label, text.c_str());
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_flex_grow(label, 1);

        lv_obj_t* button = lv_button_create(row);
        lv_obj_t* button_label = lv_label_create(button);
        lv_label_set_text(button_label, LV_SYMBOL_TRASH);
        lv_obj_set_user_data(button, reinterpret_cast<void*>(static_cast<uintptr_t>(index)));
        lv_obj_add_event_cb(button, onRemoveTrustedClientPressed, LV_EVENT_SHORT_CLICKED, ctx);
    }
}

std::vector<TrustedClient> loadTrustedClients() {
    std::vector<TrustedClient> clients;
    development_server_for_each_trusted_client([](const char* fingerprint, const char* name, void* context) {
        static_cast<std::vector<TrustedClient>*>(context)->push_back({ fingerprint, name });
        return true;
    }, &clients);
    return clients;
}

/** Shows the trusted clients when they changed */
void refreshTrustedClients(Context* ctx, WindowId window) {
    std::vector<TrustedClient> clients = loadTrustedClients();
    lvgl_lock();
    if (clients != ctx->trustedClients) {
        ctx->trustedClients = std::move(clients);
        if (window_manager_get_state(window) == WINDOW_STATE_GRANTED) {
            rebuildTrustedClients(ctx);
        }
    }
    lvgl_unlock();
}

void removePendingTrustedClients(Context* ctx) {
    mutex_lock(&ctx->removalMutex);
    std::vector<std::string> removals = std::move(ctx->pendingRemovals);
    ctx->pendingRemovals.clear();
    mutex_unlock(&ctx->removalMutex);
    for (const auto& fingerprint : removals) {
        const error_t error = development_server_remove_trusted_client(fingerprint.c_str());
        if (error != ERROR_NONE && error != ERROR_NOT_FOUND) {
            LOG_E(TAG, "Failed to remove trusted client");
        }
    }
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    lv_obj_t* toolbar = lvgl_toolbar_create(parent, tr("Development"));
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    ctx->enableSwitch = lvgl_toolbar_add_switch_action(toolbar);
    lv_obj_add_event_cb(ctx->enableSwitch, onEnableSwitchChanged, LV_EVENT_VALUE_CHANGED, ctx);

    if (development_server_is_enabled()) {
        lv_obj_add_state(ctx->enableSwitch, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(ctx->enableSwitch, LV_STATE_CHECKED);
    }

    // Wrappers

    lv_obj_t* content_wrapper = lv_obj_create(parent);
    lv_obj_set_width(content_wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(content_wrapper, 1);
    lv_obj_set_flex_flow(content_wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_border_width(content_wrapper, 0, LV_STATE_DEFAULT);
    lvgl::obj_set_style_bg_invisible(content_wrapper);

    // Status

    auto* status_card = lvgl_card_create(content_wrapper);
    lv_obj_set_size(status_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(status_card, LV_FLEX_FLOW_COLUMN);

    ctx->statusLabel = lv_label_create(status_card);
    lv_obj_set_width(ctx->statusLabel, LV_PCT(100));
    lv_label_set_long_mode(ctx->statusLabel, LV_LABEL_LONG_WRAP);

    // Trusted clients

    lv_obj_t* trusted_label = lv_label_create(content_wrapper);
    lv_label_set_text(trusted_label, tr("Trusted"));

    ctx->trustedCard = lvgl_card_create(content_wrapper);
    lv_obj_set_size(ctx->trustedCard, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ctx->trustedCard, LV_FLEX_FLOW_COLUMN);
    lvgl_grid_navigation_add(ctx->trustedCard);
    rebuildTrustedClients(ctx);

    updateViewState(ctx);
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    Context ctx {};
    ctx.appInstanceId = appInstanceId;
    mutex_construct(&ctx.removalMutex);
    ctx.trustedClients = loadTrustedClients();

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);
    check(task_event_group_claim_bit(&event_group, &ctx.enableBit) == ERROR_NONE);
    check(task_event_group_claim_bit(&event_group, &ctx.removeBit) == ERROR_NONE);
    ctx.eventGroup = &event_group;

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);

    ctx.timer = std::make_unique<Timer>(Timer::Type::Periodic, pdMS_TO_TICKS(1000), [&ctx, window] {
        if (lvgl_is_running()) {
            lvgl_lock();
            // Widgets only exist while this window is topmost - skip otherwise. Another app
            // (started non-modally, e.g. via app_start()) can bury this window without
            // stopping this instance or notifying it; window_manager deletes a buried window's
            // widgets, so touching ctx->statusLabel here would use-after-free it.
            if (window_manager_get_state(window) == WINDOW_STATE_GRANTED) {
                updateViewState(&ctx);
            }
            lvgl_unlock();
        }
    });
    ctx.timer->start();

    bool shouldClose = false;
    while (!shouldClose) {
        uint32_t flags = 0;
        // The timeout picks up clients that were trusted while this app is open
        task_event_group_wait_any(&event_group, &flags, TRUSTED_CLIENTS_REFRESH_INTERVAL);

        if ((flags & ctx.enableBit) != 0) {
            const bool enable = ctx.enableRequested;
            if (enable != development_server_is_enabled() && development_server_set_enabled(enable) != ERROR_NONE) {
                LOG_E(TAG, "Failed to change the development server");
            }
        }

        if ((flags & ctx.removeBit) != 0) {
            removePendingTrustedClients(&ctx);
        }
        refreshTrustedClients(&ctx, window);

        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            switch (event.type) {
                case APP_EVENT_CLOSE:
                    shouldClose = true;
                    break;
                default:
                    break;
            }
            if (shouldClose) break;
        }
    }

    // Equivalent of the old model's onHide(): ensure the periodic update isn't already happening.
    lvgl_lock();
    ctx.timer->stop();
    lvgl_unlock();

    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_release_bit(&event_group, ctx.enableBit);
    task_event_group_release_bit(&event_group, ctx.removeBit);
    task_event_group_destruct(&event_group);
    mutex_destruct(&ctx.removalMutex);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "tactility.development",
    .name = "Development",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {}
};

} // namespace
