#define L10N_APP "qrcode"
#include <Tactility/i18n/AppDomain.h>
#include <Tactility/app/qrcode/QrCode.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/log.h>
#include <tactility/memory.h>

#include <lvgl/lvgl.h>
#include <lvgl/widgets/qr.h>

namespace tt::app::qrcode {

constexpr auto* TAG = "QrCode";

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;
    // Set once in appMain() from its own argc/argv parameters, read by createWidgets()
    int argc = 0;
    char** argv = nullptr;
};

// A touch, a key press or a step of an encoder that is handled as keys
void onInput(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void forEachInputDevice(void (*action)(lv_indev_t* indev, Context* ctx), Context* ctx) {
    for (lv_indev_t* indev = lv_indev_get_next(nullptr); indev != nullptr; indev = lv_indev_get_next(indev)) {
        action(indev, ctx);
    }
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_scrollable(parent, false);
    lv_obj_set_style_pad_all(parent, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(parent, 0, LV_STATE_DEFAULT);

    auto* qr = lvgl_qr_create(parent);
    lv_obj_set_size(qr, LV_PCT(100), LV_PCT(100));
    const char* text = (ctx->argc > 0) ? ctx->argv[0] : "";
    const error_t error = lvgl_qr_set_data(qr, text);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to create the QR code: %s", error_to_string(error));
        auto* label = lv_label_create(parent);
        lv_label_set_text(label, tr("The data doesn't fit in a QR code"));
        lv_obj_center(label);
    }

    // Any input device can close it, whichever widget has the focus
    forEachInputDevice([](lv_indev_t* indev, Context* ctx) {
        lv_indev_add_event_cb(indev, onInput, LV_EVENT_PRESSED, ctx);
    }, ctx);
}

void destroyWidgets(void* userData) {
    forEachInputDevice([](lv_indev_t* indev, Context* ctx) {
        lv_indev_remove_event_cb_with_user_data(indev, onInput, ctx);
    }, static_cast<Context*>(userData));
}

int32_t appMain(int argc, char* argv[]) {
    Context ctx {};
    ctx.appInstanceId = app_scheduler_current_app_id();
    ctx.argc = argc;
    ctx.argv = argv;

    TaskEventGroup eventGroup {};
    task_event_group_construct(&eventGroup);
    AppEventSubscription appEvents {};
    check(app_event_subscribe(&appEvents, &eventGroup) == ERROR_NONE);

    WindowId window = window_manager_create_ext(ctx.appInstanceId, createWidgets, destroyWidgets, &ctx);

    bool shouldClose = false;
    while (!shouldClose) {
        task_event_group_wait_any(&eventGroup, nullptr, portMAX_DELAY);
        AppEvent event {};
        while (app_event_poll(&appEvents, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_CLOSE) {
                shouldClose = true;
                break;
            }
        }
    }

    window_manager_remove(window);
    check(app_event_unsubscribe(&appEvents) == ERROR_NONE);
    task_event_group_destruct(&eventGroup);
    return 0;
}

} // namespace

AppInstanceId start(const std::string& text) {
    const char* argv[] = { text.c_str() };
    AppInstanceId instanceId = 0;
    AppStartContext context = app_start_context_for_manifest(&manifest);
    app_start_context_set_arguments_ext(&context, 1, argv);
    if (app_start_with_context(&context, &instanceId) != ERROR_NONE) {
        return 0;
    }
    return instanceId;
}

extern const ::AppManifest manifest = {
    .id = "tactility.qrcode",
    .name = "QR Code",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    // The app task does no file operations, so its stack can be in external RAM
    .stack = { .depth = 0, .desired_memory_capability = MEMORY_CAPABILITY_EXTERNAL }
};

}
