#include <Tactility/app/inputdialog/InputDialog.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

#include <lvgl/widgets/toolbar.h>
#include <tactility/log.h>

#include <lvgl.h>

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
    int32_t result = 1; // Cancelled - safety-net default if closed without pressing a button
};

struct ButtonContext {
    Context* ctx;
    /** Non-null for OK (read at press time), NULL for Cancel. */
    lv_obj_t* textarea;
};

// The last text entered via OK. Static rather than per-instance: simple, and in practice only
// one InputDialog is ever open at a time. Written on the LVGL thread (onButtonPressed(), before
// emitting APP_EVENT_CLOSE); read by the parent via getLastText() after receiving that event -
// safe without a lock for the same reason Context::result is (see AlertDialog.cpp).
std::string lastText;

void onButtonDeleted(lv_event_t* e) {
    delete static_cast<ButtonContext*>(lv_event_get_user_data(e));
}

void onButtonPressed(lv_event_t* e) {
    auto* btnCtx = static_cast<ButtonContext*>(lv_event_get_user_data(e));
    if (btnCtx->textarea != nullptr) {
        LOG_I(TAG, "OK pressed");
        lastText = lv_textarea_get_text(btnCtx->textarea);
        btnCtx->ctx->result = 0;
    } else {
        LOG_I(TAG, "Cancel pressed");
        btnCtx->ctx->result = 1;
    }
    // Async, non-blocking - see AlertDialog.cpp's onButtonPressed() for why this must not
    // call app_manager_stop() directly (would deadlock against the LVGL lock).
    AppEvent event { .type = APP_EVENT_CLOSE, .timestamp = 0, .result = {} };
    app_event_emit(btnCtx->ctx->appInstanceId, &event);
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

int32_t appMain(uint32_t appInstanceId, int argc, char* argv[]) {
    Context ctx { appInstanceId };
    ctx.argc = argc;
    ctx.argv = argv;

    AppEventSubscription sub {};
    sub.app_instance_id = appInstanceId;
    app_event_subscribe(&sub);

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);

    while (true) {
        AppEvent event {};
        if (app_event_await(&sub, &event, portMAX_DELAY) != ERROR_NONE) {
            break;
        }
        if (event.type == APP_EVENT_CLOSE) {
            app_manager_finish(appInstanceId); // no-op: modal children never supersede anything
            break;
        }
    }

    window_manager_remove(window);
    app_event_unsubscribe(&sub);

    return ctx.result;
}

} // namespace

uint32_t start(uint32_t callerAppInstanceId, const std::string& title, const std::string& message, const std::string& prefilled) {
    const char* argv[] = { title.c_str(), message.c_str(), prefilled.c_str() };
    uint32_t instanceId = 0;
    app_manager_start_for_result(manifest.id, callerAppInstanceId, 3, argv, &instanceId);
    return instanceId;
}

std::string getLastText() {
    return lastText;
}

extern const ::AppManifest manifest = {
    .id = "InputDialog",
    .name = "Input Dialog",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
};

}
