#include <Tactility/app/inputdialog/InputDialog.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>
#include <app/stream.h>

#include <lvgl_window_manager/window_manager.h>

#include <lvgl/widgets/toolbar.h>
#include <tactility/check.h>
#include <tactility/log.h>

#include <lvgl.h>

#include <unistd.h>

namespace tt::app::inputdialog {

constexpr auto* TAG = "InputDialog";

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;
    // Set once in appMain() from its own argc/argv parameters, read by createWidgets() - see
    // AlertDialog.cpp's Context::argc/argv for why this is safe without a lock.
    int argc = 0;
    char** argv = nullptr;
    // The eventual appMain() return value - see AlertDialog.cpp's Context::result for why this
    // is a plain (non-atomic) field safely shared between the LVGL thread (writer, before
    // emitting APP_EVENT_CLOSE) and this dialog's own thread (reader, after waking from it).
    int32_t resultCode = 1; // Cancelled - safety-net default if closed without pressing a button
    std::string resultText;
};

struct ButtonContext {
    Context* ctx;
    /** Non-null for OK (read at press time), NULL for Cancel. */
    lv_obj_t* textarea;
};

void onButtonDeleted(lv_event_t* e) {
    delete static_cast<ButtonContext*>(lv_event_get_user_data(e));
}

void onButtonPressed(lv_event_t* e) {
    auto* btnCtx = static_cast<ButtonContext*>(lv_event_get_user_data(e));
    if (btnCtx->textarea != nullptr) {
        LOG_I(TAG, "OK pressed");
        btnCtx->ctx->resultText = lv_textarea_get_text(btnCtx->textarea);
        btnCtx->ctx->resultCode = 0;
    } else {
        LOG_I(TAG, "Cancel pressed");
        btnCtx->ctx->resultCode = 1;
    }
    app_event_emit_close(btnCtx->ctx->appInstanceId);
}

void createButton(Context* ctx, lv_obj_t* parent, const std::string& text, lv_obj_t* textarea) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_t* button_label = lv_label_create(button);
    lv_obj_align(button_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(button_label, text.c_str());
    auto* btnCtx = new ButtonContext { ctx, textarea };
    lv_obj_add_event_cb(button, onButtonPressed, LV_EVENT_SHORT_CLICKED, btnCtx);
    lv_obj_add_event_cb(button, onButtonDeleted, LV_EVENT_DELETE, btnCtx);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    // argv layout: [0]=title, [1]=message, [2]=prefilled.
    char** argv = ctx->argv;

    auto* toolbar = lvgl_toolbar_create(parent, argv[0]);
    lv_obj_align(toolbar, LV_ALIGN_TOP_MID, 0, 0);

    auto* message_label = lv_label_create(parent);
    lv_obj_align(message_label, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_width(message_label, LV_PCT(80));
    lv_label_set_text(message_label, argv[1]);
    lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP);

    auto* textarea = lv_textarea_create(parent);
    lv_obj_align_to(textarea, message_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
    lv_textarea_set_one_line(textarea, true);
    if (argv[2][0] != '\0') {
        lv_textarea_set_text(textarea, argv[2]);
    }

    auto* button_wrapper = lv_obj_create(parent);
    lv_obj_set_flex_flow(button_wrapper, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(button_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(button_wrapper, 0, 0);
    lv_obj_set_flex_align(button_wrapper, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_width(button_wrapper, 0, 0);
    lv_obj_align(button_wrapper, LV_ALIGN_BOTTOM_MID, 0, -4);

    createButton(ctx, button_wrapper, "OK", textarea);
    createButton(ctx, button_wrapper, "Cancel", nullptr);
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

    if (ctx.resultCode == 0) {
        // The caller captures this via an AppStream bound to our stdout (see start()); see
        // AppStdioWrap.cpp for how printf() itself gets routed there on POSIX.
        printf("%s", ctx.resultText.c_str());
    }
    return ctx.resultCode;
}

} // namespace

uint32_t start(uint32_t callerAppInstanceId, const std::string& title, const std::string& message, const std::string& prefilled, AppStream& stream, void* buffer, size_t bufferCapacity, TaskEventGroup* eventGroup) {
    const char* argv[] = { title.c_str(), message.c_str(), prefilled.c_str() };
    AppStreamBinding binding = {
        .producer_fd = STDOUT_FILENO,
        .stream = &stream,
        .buffer = buffer,
        .buffer_capacity = bufferCapacity,
        .event_group = eventGroup,
    };
    uint32_t instanceId = 0;
    app_start_for_result_with_streams(manifest.id, 3, argv, &binding, 1, callerAppInstanceId, &instanceId);
    return instanceId;
}

extern const ::AppManifest manifest = {
    .id = "tactility.inputdialog",
    .name = "Input Dialog",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = {}
};

}
