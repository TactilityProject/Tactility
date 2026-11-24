#define L10N_APP "developmentprompt"
#include <Tactility/i18n/AppDomain.h>
#include <Tactility/app/dialog/Dialog.h>
#include <Tactility/app/development/Fingerprint.h>
#include <Tactility/lvgl/Style.h>

#include <development/server.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/log.h>

#include <lvgl.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/widgets/toolbar.h>

#include <format>
#include <string>

namespace tt::app::developmentprompt {

constexpr auto* TAG = "DevelopmentPrompt";

namespace {

struct Context {
    uint32_t appInstanceId = 0;
    // argv: see DEVELOPMENT_SERVER_PROMPT_APP_ID
    char** argv = nullptr;
    TaskEventGroup* eventGroup = nullptr;
    uint32_t detailsBit = 0;
    // Written by the button callbacks before they emit APP_EVENT_CLOSE, so a dismissed prompt rejects
    DevelopmentPromptResult result = DEVELOPMENT_PROMPT_REJECT;
};

void onAcceptPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    ctx->result = DEVELOPMENT_PROMPT_ACCEPT_ALWAYS;
    app_event_emit_close(ctx->appInstanceId);
}

void onRejectPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    ctx->result = DEVELOPMENT_PROMPT_REJECT;
    app_event_emit_close(ctx->appInstanceId);
}

void onDetailsPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    task_event_group_signal(ctx->eventGroup, ctx->detailsBit);
}

lv_obj_t* createButton(lv_obj_t* parent, const char* text, lv_event_cb_t callback, Context* ctx) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_add_event_cb(button, callback, LV_EVENT_SHORT_CLICKED, ctx);
    return button;
}

lv_obj_t* createRow(lv_obj_t* parent) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_scrollable(row, false);
    lvgl::obj_set_style_bg_invisible(row);
    lv_obj_set_style_border_width(row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(row, 0, LV_STATE_DEFAULT);
    return row;
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    char** argv = ctx->argv;

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    lvgl_toolbar_create(parent, tr("Remote access"));

    lv_obj_t* content = lv_obj_create(parent);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_border_width(content, 0, LV_STATE_DEFAULT);
    lvgl::obj_set_style_bg_invisible(content);

    lv_obj_t* message_label = lv_label_create(content);
    lv_obj_set_width(message_label, LV_PCT(100));
    lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP);
    const std::string message = i18n::format(tr("{} at {} requests {} access."), { argv[1], argv[0], argv[3] });
    lv_label_set_text(message_label, message.c_str());

    lv_obj_t* fingerprint_row = createRow(content);
    lv_obj_set_flex_align(fingerprint_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t* fingerprint_label = lv_label_create(fingerprint_row);
    const std::string fingerprint = i18n::format(tr("Fingerprint: {}"), { development::formatShortFingerprint(argv[2]) });
    lv_label_set_text(fingerprint_label, fingerprint.c_str());
    lv_label_set_long_mode(fingerprint_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_grow(fingerprint_label, 1);
    createButton(fingerprint_row, "...", onDetailsPressed, ctx);

    lv_obj_t* button_row = createRow(parent);
    lv_obj_set_flex_align(button_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(button_row, 12, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_ver(button_row, 4, LV_STATE_DEFAULT);
    createButton(button_row, tr("Accept"), onAcceptPressed, ctx);
    createButton(button_row, tr("Reject"), onRejectPressed, ctx);
    lvgl_grid_navigation_add(button_row);
}

int32_t appMain(int argc, char* argv[]) {
    if (argc != 4) {
        LOG_E(TAG, "Expected 4 arguments, got %d", argc);
        return DEVELOPMENT_PROMPT_REJECT;
    }

    Context ctx {};
    ctx.appInstanceId = app_scheduler_current_app_id();
    ctx.argv = argv;

    TaskEventGroup eventGroup {};
    task_event_group_construct(&eventGroup);
    AppEventSubscription subscription {};
    check(app_event_subscribe(&subscription, &eventGroup) == ERROR_NONE);
    check(task_event_group_claim_bit(&eventGroup, &ctx.detailsBit) == ERROR_NONE);
    ctx.eventGroup = &eventGroup;

    WindowId window = window_manager_create(ctx.appInstanceId, createWidgets, &ctx);

    bool done = false;
    while (!done) {
        uint32_t flags = 0;
        task_event_group_wait_any(&eventGroup, &flags, portMAX_DELAY);

        if (flags & ctx.detailsBit) {
            // Refused while the dialog is already shown
            dialog::alert(ctx.appInstanceId, tr("Fingerprint"), development::formatFingerprint(argv[2]));
        }

        AppEvent event {};
        while (app_event_poll(&subscription, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_CLOSE) {
                done = true;
            }
        }
    }

    window_manager_remove(window);
    check(app_event_unsubscribe(&subscription) == ERROR_NONE);
    task_event_group_release_bit(&eventGroup, ctx.detailsBit);
    task_event_group_destruct(&eventGroup);
    return ctx.result;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = DEVELOPMENT_SERVER_PROMPT_APP_ID,
    .name = "Development Prompt",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = {}
};

} // namespace tt::app::developmentprompt
