#include <Tactility/app/timedatesettings/TimeDateSettings.h>
#include <Tactility/app/timezone/TimeZone.h>
#include <Tactility/settings/SystemSettings.h>
#include <Tactility/settings/Time.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/log.h>

#include <lvgl/lvgl.h>
#include <lvgl/widgets/toolbar.h>

namespace tt::app::timedatesettings {

constexpr auto* TAG = "TimeDate";

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;
    lv_obj_t* timeZoneLabel = nullptr;
    lv_obj_t* dateFormatDropdown = nullptr;
    uint32_t pendingTimeZoneDialogId = 0;
};


void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    // Async, non-blocking - must NOT call app_manager_stop() directly here: that bound-waits
    // (thread_join) for this app's own thread to finish, which needs the LVGL lock
    // (window_manager_remove()) - but this callback runs ON the LVGL task, which would
    // deadlock against itself.
    AppEvent closeEvent { .type = APP_EVENT_CLOSE, .timestamp = 0, .result = {} };
    app_event_emit(ctx->appInstanceId, &closeEvent);
}

void onTimeFormatChanged(lv_event_t* event) {
    auto* widget = lv_event_get_target_obj(event);
    bool show_24 = lv_obj_has_state(widget, LV_STATE_CHECKED);
    settings::setTimeFormat24Hour(show_24);
}

void onTimeZonePressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    ctx->pendingTimeZoneDialogId = timezone::start(ctx->appInstanceId, true);
}

void onDateFormatChanged(lv_event_t* event) {
    auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(event));
    auto index = lv_dropdown_get_selected(dropdown);

    const char* dateFormats[] = {"MM/DD/YYYY", "DD/MM/YYYY", "YYYY-MM-DD", "YYYY/MM/DD"};
    std::string selected_format = dateFormats[index];

    settings::SystemSettings sysSettings;
    if (settings::loadSystemSettings(sysSettings)) {
        sysSettings.dateFormat = selected_format;
        settings::saveSystemSettings(sysSettings);
    }
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "Time & Date");
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    auto* main_wrapper = lv_obj_create(parent);
    lv_obj_set_flex_flow(main_wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(main_wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(main_wrapper, 1);

    // 24-hour format toggle

    auto* time_format_wrapper = lv_obj_create(main_wrapper);
    lv_obj_set_width(time_format_wrapper, LV_PCT(100));
    lv_obj_set_height(time_format_wrapper, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(time_format_wrapper, 8, 0);
    lv_obj_set_style_border_width(time_format_wrapper, 0, 0);

    auto* time_24h_label = lv_label_create(time_format_wrapper);
    lv_label_set_text(time_24h_label, "24-hour format");
    lv_obj_align(time_24h_label, LV_ALIGN_LEFT_MID, 4, 0);

    auto* time_24h_switch = lv_switch_create(time_format_wrapper);
    lv_obj_align(time_24h_switch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(time_24h_switch, onTimeFormatChanged, LV_EVENT_VALUE_CHANGED, nullptr);
    if (settings::isTimeFormat24Hour()) {
        lv_obj_add_state(time_24h_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(time_24h_switch, LV_STATE_CHECKED);
    }

    // Date format dropdown

    auto* date_format_wrapper = lv_obj_create(main_wrapper);
    lv_obj_set_width(date_format_wrapper, LV_PCT(100));
    lv_obj_set_height(date_format_wrapper, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(date_format_wrapper, 8, 0);
    lv_obj_set_style_border_width(date_format_wrapper, 0, 0);

    auto* date_format_label = lv_label_create(date_format_wrapper);
    lv_label_set_text(date_format_label, "Date format");
    lv_obj_align(date_format_label, LV_ALIGN_LEFT_MID, 4, 0);

    ctx->dateFormatDropdown = lv_dropdown_create(date_format_wrapper);
    lv_obj_set_width(ctx->dateFormatDropdown, 150);
    lv_obj_align(ctx->dateFormatDropdown, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_dropdown_set_options(ctx->dateFormatDropdown, "MM/DD/YYYY\nDD/MM/YYYY\nYYYY-MM-DD\nYYYY/MM/DD");

    settings::SystemSettings sysSettings;
    if (settings::loadSystemSettings(sysSettings)) {
        int index = 0;
        if (sysSettings.dateFormat == "DD/MM/YYYY") index = 1;
        else if (sysSettings.dateFormat == "YYYY-MM-DD") index = 2;
        else if (sysSettings.dateFormat == "YYYY/MM/DD") index = 3;
        lv_dropdown_set_selected(ctx->dateFormatDropdown, index);
    }
    lv_obj_add_event_cb(ctx->dateFormatDropdown, onDateFormatChanged, LV_EVENT_VALUE_CHANGED, nullptr);

    // Timezone selector

    auto* timezone_wrapper = lv_obj_create(main_wrapper);
    lv_obj_set_width(timezone_wrapper, LV_PCT(100));
    lv_obj_set_height(timezone_wrapper, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(timezone_wrapper, 8, 0);
    lv_obj_set_style_border_width(timezone_wrapper, 0, 0);

    auto* timezone_label = lv_label_create(timezone_wrapper);
    lv_label_set_text(timezone_label, "Timezone");
    lv_obj_align(timezone_label, LV_ALIGN_LEFT_MID, 4, 0);

    auto* timezone_button = lv_button_create(timezone_wrapper);
    lv_obj_set_width(timezone_button, 150);
    lv_obj_align(timezone_button, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(timezone_button, onTimeZonePressed, LV_EVENT_SHORT_CLICKED, ctx);

    ctx->timeZoneLabel = lv_label_create(timezone_button);
    std::string timeZoneName = settings::getTimeZoneName();
    if (timeZoneName.empty()) {
        timeZoneName = "not set";
    }
    lv_obj_center(ctx->timeZoneLabel);
    lv_label_set_text(ctx->timeZoneLabel, timeZoneName.c_str());
}

int32_t appMain(uint32_t appInstanceId, int argc, char* argv[]) {
    Context ctx {};
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
            case APP_EVENT_RESULT:
                if (event.result.launch_id == ctx.pendingTimeZoneDialogId) {
                    ctx.pendingTimeZoneDialogId = 0;
                    if (event.result.result == 0 /* Ok */) {
                        const auto name = timezone::getLastName();
                        LOG_I(TAG, "Result name=%s code=%s", name.c_str(), timezone::getLastCode().c_str());
                        if (!name.empty()) {
                            lvgl_lock();
                            lv_label_set_text(ctx.timeZoneLabel, name.c_str());
                            lvgl_unlock();
                        }
                    }
                }
                app_manager_stop(event.result.launch_id);
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

uint32_t start() {
    uint32_t instanceId = 0;
    app_manager_start(manifest.id, &instanceId);
    return instanceId;
}

extern const ::AppManifest manifest = {
    .id = "TimeDateSettings",
    .name = "Time & Date",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) }
};

} // namespace tt::app::timedatesettings
