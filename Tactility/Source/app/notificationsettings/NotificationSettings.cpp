// SPDX-License-Identifier: Apache-2.0
#define L10N_APP "notificationsettings"
#include <Tactility/i18n/AppDomain.h>
#include <Tactility/lvgl/NotificationSource.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <lvgl_window_manager/window_manager.h>

#include <notification/notification.h>

#include <tactility/check.h>
#include <tactility/concurrent/task_event_group.h>
#include <tactility/log.h>
#include <tactility/std/string.hpp>
#include <tactility/std/vector.hpp>

#include <lvgl/grid_navigation.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/toolbar.h>

#include <cstring>

namespace tt::app::notificationsettings {

extern const ::AppManifest manifest;

namespace {

constexpr auto* TAG = "NotificationSettings";

struct Context {
    uint32_t appInstanceId;
    TaskEventGroup* eventGroup = nullptr;
    uint32_t applyBit = 0;
    /** The senders that sent a notification at least once. Filled by the app's task, changed by the switches. */
    tt::std::vector<NotificationSource> sources;
    tt::std::vector<tt::std::string> names;
    /** What the module has, so that the app's task only applies what changed. The LVGL lock guards both. */
    tt::std::vector<bool> applied;
};

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void onSwitchChanged(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* sw = lv_event_get_target_obj(event);
    const auto index = static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(sw)));
    if (index < ctx->sources.size()) {
        ctx->sources[index].enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
        // Saving is a file operation, which the app's task does
        task_event_group_signal(ctx->eventGroup, ctx->applyBit);
    }
}

lv_obj_t* createRow(lv_obj_t* parent) {
    auto* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(row, false);
    return row;
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, tr("Notifications"));
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    auto* wrapper = lv_obj_create(parent);
    lv_obj_set_style_border_width(wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(wrapper, 1);
    lv_obj_set_style_pad_row(wrapper, 8, LV_STATE_DEFAULT);

    if (ctx->sources.empty()) {
        auto* label = lv_label_create(wrapper);
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
        lv_label_set_text(label, tr("No app has sent a notification yet."));
        return;
    }

    auto* hint = lv_label_create(wrapper);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_label_set_text(hint, tr("Turning an app off also removes its notifications."));

    auto* card = lvgl_card_create(wrapper);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 8, LV_STATE_DEFAULT);

    for (size_t i = 0; i < ctx->sources.size(); i++) {
        auto* row = createRow(card);
        auto* label = lv_label_create(row);
        lv_obj_set_flex_grow(label, 1);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text(label, ctx->names[i].c_str());

        auto* sw = lv_switch_create(row);
        lv_obj_set_user_data(sw, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
        lv_obj_set_state(sw, LV_STATE_CHECKED, ctx->sources[i].enabled);
        lv_obj_add_event_cb(sw, onSwitchChanged, LV_EVENT_VALUE_CHANGED, ctx);
        // The arrow keys would toggle a switch that is in the input group itself
        lvgl_grid_navigation_add(row);
    }
}

/** Reads what the module has. Reads storage the first time, so it is called without the LVGL lock. */
void loadSources(Context* ctx) {
    ctx->sources.assign(NOTIFICATION_SOURCE_LIMIT, NotificationSource {});
    size_t count = 0;
    if (notification_get_sources(ctx->sources.data(), NOTIFICATION_SOURCE_LIMIT, &count) != ERROR_NONE) {
        count = 0;
    }
    ctx->sources.resize(count);
    ctx->names.clear();
    ctx->applied.clear();
    ::std::vector<::std::string> friendly;
    for (const auto& source : ctx->sources) {
        friendly.push_back(lvgl::getNotificationSourceName(source.id));
        ctx->applied.push_back(source.enabled);
    }
    // Senders that end up with the same name get the full id, and the type when the id is shared by an app and a service
    for (size_t i = 0; i < ctx->sources.size(); i++) {
        ::std::string name = friendly[i];
        bool sameName = false;
        bool sameId = false;
        for (size_t j = 0; j < ctx->sources.size(); j++) {
            if (j != i && friendly[j] == friendly[i]) {
                sameName = true;
                sameId |= strcmp(ctx->sources[j].id, ctx->sources[i].id) == 0;
            }
        }
        if (sameName) {
            name += ::std::string(" (") + ctx->sources[i].id + ")";
        }
        if (sameId) {
            name += ctx->sources[i].type == NOTIFICATION_SOURCE_APP ? ::std::string(" ") + tr("app") : ::std::string(" ") + tr("service");
        }
        ctx->names.emplace_back(name.c_str());
    }
}

/** Sends the switches that changed to the module */
void applyChanges(Context* ctx) {
    for (size_t i = 0; i < ctx->sources.size(); i++) {
        lvgl_lock();
        const bool enabled = ctx->sources[i].enabled;
        const bool changed = enabled != ctx->applied[i];
        if (changed) {
            ctx->applied[i] = enabled;
        }
        lvgl_unlock();
        if (changed) {
            const error_t error = notification_set_source_enabled(ctx->sources[i].type, ctx->sources[i].id, enabled);
            if (error != ERROR_NONE) {
                LOG_E(TAG, "Failed to change %s: %s", ctx->sources[i].id, error_to_string(error));
            }
        }
    }
}

int32_t appMain(int argc, char* argv[]) {
    Context ctx {};
    ctx.appInstanceId = app_scheduler_current_app_id();

    TaskEventGroup eventGroup {};
    task_event_group_construct(&eventGroup);
    ctx.eventGroup = &eventGroup;
    check(task_event_group_claim_bit(&eventGroup, &ctx.applyBit) == ERROR_NONE);
    loadSources(&ctx);

    AppEventSubscription appEvents {};
    check(app_event_subscribe(&appEvents, &eventGroup) == ERROR_NONE);

    WindowId window = window_manager_create(ctx.appInstanceId, createWidgets, &ctx);
    if (window == 0) {
        // Without a window the app has no way to close, so it ends here
        app_event_unsubscribe(&appEvents);
        task_event_group_destruct(&eventGroup);
        return -1;
    }

    bool shouldClose = false;
    while (!shouldClose) {
        uint32_t flags = 0;
        task_event_group_wait_any(&eventGroup, &flags, portMAX_DELAY);

        if (flags & ctx.applyBit) {
            applyChanges(&ctx);
        }

        AppEvent event {};
        while (app_event_poll(&appEvents, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_CLOSE) {
                shouldClose = true;
                break;
            }
        }
    }

    // Removing the window waits for its widgets, so no switch can signal the destroyed event group
    window_manager_remove(window);
    applyChanges(&ctx);
    check(app_event_unsubscribe(&appEvents) == ERROR_NONE);
    task_event_group_destruct(&eventGroup);
    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "tactility.notificationsettings",
    .name = "Notifications",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {}
};

} // namespace tt::app::notificationsettings
