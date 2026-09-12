#include "Tactility/app/alertdialog/AlertDialog.h"

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/log.h>

#include <lvgl.h>
#include <lvgl/widgets/toolbar.h>

namespace tt::app::alertdialog {

constexpr auto* TAG = "AlertDialog";

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;
    // Set once in appMain() from its own argc/argv parameters, read by createWidgets() (which
    // may run on a different task - the LVGL task, or another app's task via
    // window_manager_remove()'s cross-thread rebuild-on-remove path). Safe to hold onto without a
    // lock: the deep copy stays valid for exactly as long as appMain() is running, which is
    // longer than createWidgets() ever needs it.
    int argc = 0;
    char** argv = nullptr;
    // The eventual appMain() return value (= this dialog's APP_EVENT_RESULT result code) -
    // written here by onButtonPressed() (LVGL thread) before it emits APP_EVENT_CLOSE, read by
    // appMain() (this dialog's own thread) after waking from that event. No atomic/lock needed:
    // the emit/await pair between the two already establishes happens-before ordering, same as
    // every other cross-thread Context field write in this codebase's converted apps.
    int32_t result = 1; // Cancelled - safety-net default if closed without pressing a button
};

struct ButtonContext {
    Context* ctx;
    int32_t index;
};

void onButtonDeleted(lv_event_t* e) {
    delete static_cast<ButtonContext*>(lv_event_get_user_data(e));
}

void onButtonPressed(lv_event_t* e) {
    auto* btnCtx = static_cast<ButtonContext*>(lv_event_get_user_data(e));
    LOG_I(TAG, "Selected item at index %d", (int)btnCtx->index);
    btnCtx->ctx->result = btnCtx->index;
    app_event_emit_close(btnCtx->ctx->appInstanceId);
}

void createButton(Context* ctx, lv_obj_t* parent, const std::string& text, int32_t index) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_t* button_label = lv_label_create(button);
    lv_obj_align(button_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(button_label, text.c_str());
    auto* btnCtx = new ButtonContext { ctx, index };
    lv_obj_add_event_cb(button, onButtonPressed, LV_EVENT_SHORT_CLICKED, btnCtx);
    lv_obj_add_event_cb(button, onButtonDeleted, LV_EVENT_DELETE, btnCtx);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    // argv layout: [0]=title, [1]=message, [2..argc)=button labels.
    int argc = ctx->argc;
    char** argv = ctx->argv;

    lv_obj_t* toolbar = lvgl_toolbar_create(parent, argv[0]);
    lv_obj_align(toolbar, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t* message_label = lv_label_create(parent);
    lv_obj_align(message_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_width(message_label, LV_PCT(80));
    lv_obj_set_style_text_align(message_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(message_label, argv[1]);
    lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP);

    lv_obj_t* button_wrapper = lv_obj_create(parent);
    lv_obj_set_flex_flow(button_wrapper, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(button_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(button_wrapper, 0, 0);
    lv_obj_set_flex_align(button_wrapper, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_width(button_wrapper, 0, 0);
    lv_obj_align(button_wrapper, LV_ALIGN_BOTTOM_MID, 0, -4);

    for (int32_t index = 0; index < argc - 2; index++) {
        createButton(ctx, button_wrapper, argv[2 + index], index);
    }
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    Context ctx { appInstanceId };
    ctx.argc = argc;
    ctx.argv = argv;

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);

    while (true) {
        task_event_group_wait_any(&event_group, nullptr, portMAX_DELAY);

        bool shouldClose = false;
        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_CLOSE) {
                shouldClose = true;
                break;
            }
        }
        if (shouldClose) break;
    }

    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);

    return ctx.result;
}

} // namespace

namespace {

// Builds argv = [title, message, buttonLabels...] for app_start_for_result().
std::vector<const char*> buildArgv(const std::string& title, const std::string& message, const std::vector<std::string>& buttonLabels) {
    std::vector<const char*> argv { title.c_str(), message.c_str() };
    for (const auto& label: buttonLabels) {
        argv.push_back(label.c_str());
    }
    return argv;
}

} // namespace

uint32_t start(uint32_t callerAppInstanceId, const std::string& title, const std::string& message, const std::vector<std::string>& buttonLabels) {
    auto argv = buildArgv(title, message, buttonLabels);
    uint32_t instanceId = 0;
    app_start_for_result(manifest.id, static_cast<int>(argv.size()), argv.data(), callerAppInstanceId, &instanceId);
    return instanceId;
}

uint32_t start(uint32_t callerAppInstanceId, const std::string& title, const std::string& message) {
    return start(callerAppInstanceId, title, message, std::vector<std::string> { "OK" });
}

extern const ::AppManifest manifest = {
    .id = "tactility.alertdialog",
    .name = "Alert Dialog",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = {}
};

}
