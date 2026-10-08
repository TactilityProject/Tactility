#include <Tactility/app/selectiondialog/SelectionDialog.h>
#include <Tactility/app/timedatesettings/TimeDateSettings.h>
#include <Tactility/app/timezone/TimeZone.h>
#include <Tactility/settings/SystemSettings.h>
#include <Tactility/settings/Time.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/concurrent/task_event_group.h>
#include <tactility/log.h>

#include <lvgl/fonts.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/toolbar.h>

#include <string>
#include <vector>

namespace tt::app::timedatesettings {

constexpr auto* TAG = "TimeDate";

extern const ::AppManifest manifest;

namespace {

constexpr const char* DATE_FORMATS[] = { "MM/DD/YYYY", "DD/MM/YYYY", "YYYY-MM-DD", "YYYY/MM/DD" };

struct Context {
    uint32_t appInstanceId;
    TaskEventGroup* eventGroup = nullptr;
    uint32_t selectDateFormatBit = 0;
    uint32_t pendingTimeZoneDialogId = 0;
    uint32_t dateFormatDialogId = 0;
    // Valid while the window is shown
    lv_obj_t* timeZoneLabel = nullptr;
    lv_obj_t* dateFormatLabel = nullptr;
};

std::string getDateFormat() {
    settings::SystemSettings sysSettings;
    if (settings::loadSystemSettings(sysSettings)) {
        for (const auto* format : DATE_FORMATS) {
            if (sysSettings.dateFormat == format) {
                return format;
            }
        }
    }
    return DATE_FORMATS[0];
}


void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
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

void onSelectDateFormatPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    task_event_group_signal(ctx->eventGroup, ctx->selectDateFormatBit);
}

void startDateFormatSelection(Context* ctx) {
    if (ctx->dateFormatDialogId != 0) {
        return;
    }
    const std::vector<std::string> items(std::begin(DATE_FORMATS), std::end(DATE_FORMATS));
    ctx->dateFormatDialogId = selectiondialog::start(ctx->appInstanceId, "Date format", items);
}

void onDateFormatSelectionResult(Context* ctx, const AppEvent& event) {
    ctx->dateFormatDialogId = 0;
    // Other results mean that the dialog was dismissed
    const int32_t selected = event.result.result;
    if (selected >= 0 && selected < static_cast<int32_t>(std::size(DATE_FORMATS))) {
        settings::SystemSettings sysSettings;
        if (settings::loadSystemSettings(sysSettings)) {
            sysSettings.dateFormat = DATE_FORMATS[selected];
            settings::saveSystemSettings(sysSettings);
        }
        lvgl_lock();
        if (ctx->dateFormatLabel != nullptr) {
            lv_label_set_text(ctx->dateFormatLabel, getDateFormat().c_str());
        }
        lvgl_unlock();
    }
}

/**
 * Creates a title with the setting's value below it, which take the width that the row's button leaves.
 * Texts that don't fit scroll.
 * @return the value label
 */
lv_obj_t* createTitleAndValue(lv_obj_t* row, const char* title) {
    auto* texts = lv_obj_create(row);
    lv_obj_set_height(texts, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(texts, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(texts, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(texts, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(texts, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_remove_flag(texts, LV_OBJ_FLAG_SCROLLABLE);

    auto* title_label = lv_label_create(texts);
    lv_obj_set_width(title_label, LV_PCT(100));
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_label_set_text(title_label, title);

    auto* value_label = lv_label_create(texts);
    lv_obj_set_width(value_label, LV_PCT(100));
    lv_obj_set_style_text_font(value_label, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    return value_label;
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "Time & Date");
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    auto* main_wrapper = lv_obj_create(parent);
    lv_obj_set_style_border_width(main_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(main_wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(main_wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(main_wrapper, 1);

    // The rows are transparent, so the card provides the background
    auto* card = lvgl_card_create(main_wrapper);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    // 24-hour format toggle

    auto* time_format_row = lv_obj_create(card);
    lv_obj_set_size(time_format_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(time_format_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_format_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_width(time_format_row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(time_format_row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(time_format_row, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_remove_flag(time_format_row, LV_OBJ_FLAG_SCROLLABLE);

    auto* time_24h_label = lv_label_create(time_format_row);
    lv_label_set_text(time_24h_label, "24-hour format");
    lv_obj_set_flex_grow(time_24h_label, 1);

    auto* time_24h_switch = lv_switch_create(time_format_row);
    lv_obj_add_event_cb(time_24h_switch, onTimeFormatChanged, LV_EVENT_VALUE_CHANGED, nullptr);
    if (settings::isTimeFormat24Hour()) {
        lv_obj_add_state(time_24h_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(time_24h_switch, LV_STATE_CHECKED);
    }

    // "Date format        MM/DD/YYYY [Select]"

    auto* date_format_row = lv_obj_create(card);
    lv_obj_set_size(date_format_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(date_format_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(date_format_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_width(date_format_row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(date_format_row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(date_format_row, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_remove_flag(date_format_row, LV_OBJ_FLAG_SCROLLABLE);

    ctx->dateFormatLabel = createTitleAndValue(date_format_row, "Date format");
    lv_label_set_text(ctx->dateFormatLabel, getDateFormat().c_str());

    auto* date_format_button = lv_button_create(date_format_row);
    lv_label_set_text(lv_label_create(date_format_button), "Change");
    lv_obj_add_event_cb(date_format_button, onSelectDateFormatPressed, LV_EVENT_SHORT_CLICKED, ctx);

    // "Timezone           Europe/Brussels [Change]"

    auto* timezone_row = lv_obj_create(card);
    lv_obj_set_size(timezone_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(timezone_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(timezone_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_width(timezone_row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(timezone_row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(timezone_row, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_remove_flag(timezone_row, LV_OBJ_FLAG_SCROLLABLE);

    ctx->timeZoneLabel = createTitleAndValue(timezone_row, "Timezone");
    std::string timeZoneName = settings::getTimeZoneName();
    if (timeZoneName.empty()) {
        timeZoneName = "not set";
    }
    lv_label_set_text(ctx->timeZoneLabel, timeZoneName.c_str());

    auto* timezone_button = lv_button_create(timezone_row);
    lv_label_set_text(lv_label_create(timezone_button), "Change");
    lv_obj_add_event_cb(timezone_button, onTimeZonePressed, LV_EVENT_SHORT_CLICKED, ctx);
}

void destroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->timeZoneLabel = nullptr;
    ctx->dateFormatLabel = nullptr;
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    Context ctx {};
    ctx.appInstanceId = appInstanceId;

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);
    ctx.eventGroup = &event_group;
    check(task_event_group_claim_bit(&event_group, &ctx.selectDateFormatBit) == ERROR_NONE);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    WindowId window = window_manager_create_ext(appInstanceId, createWidgets, destroyWidgets, &ctx);

    bool shouldClose = false;
    while (!shouldClose) {
        uint32_t flags = 0;
        task_event_group_wait_any(&event_group, &flags, portMAX_DELAY);

        if (flags & ctx.selectDateFormatBit) {
            startDateFormatSelection(&ctx);
        }

        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            switch (event.type) {
                case APP_EVENT_CLOSE:
                    shouldClose = true;
                    break;
                case APP_EVENT_RESULT:
                    if (event.result.launch_id == ctx.dateFormatDialogId) {
                        onDateFormatSelectionResult(&ctx, event);
                    } else if (event.result.launch_id == ctx.pendingTimeZoneDialogId) {
                        ctx.pendingTimeZoneDialogId = 0;
                        if (event.result.result == 0 /* Ok */) {
                            const auto name = timezone::getLastName();
                            LOG_I(TAG, "Result name=%s code=%s", name.c_str(), timezone::getLastCode().c_str());
                            if (!name.empty()) {
                                lvgl_lock();
                                if (ctx.timeZoneLabel != nullptr) {
                                    lv_label_set_text(ctx.timeZoneLabel, name.c_str());
                                }
                                lvgl_unlock();
                            }
                        }
                    }
                    app_manager_stop(event.result.launch_id);
                    break;
                default:
                    break;
            }
            if (shouldClose) break;
        }
    }

    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);

    return 0;
}

} // namespace

uint32_t start() {
    uint32_t instanceId = 0;
    AppStartContext context = app_start_context_for_manifest(&manifest);
    app_start_with_context(&context, &instanceId);
    return instanceId;
}

extern const ::AppManifest manifest = {
    .id = "tactility.timedatesettings",
    .name = "Time & Date",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {}
};

} // namespace tt::app::timedatesettings
