#define L10N_APP "timezone"
#include <Tactility/i18n/AppDomain.h>
#include <Tactility/app/timezone/TimeZone.h>
#include <Tactility/app/timezone/TimeZoneView.h>
#include <Tactility/settings/Time.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>

#include <lvgl/widgets/toolbar.h>

#include <memory>

namespace tt::app::timezone {

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;
    bool saveTimeZone = false;
    std::unique_ptr<TimeZoneView> view;
    // The eventual appMain() return value - see AlertDialog.cpp's Context::result for why this
    // is a plain (non-atomic) field safely shared between the LVGL thread (writer, before
    // emitting APP_EVENT_CLOSE) and this app's own thread (reader, after waking from it).
    int32_t result = 1; // Cancelled - safety-net default if closed without picking a time zone
};

// The last picked name/code. Static rather than per-instance: simple, and in practice only one
// TimeZone dialog is ever open at a time. Written before emitting APP_EVENT_CLOSE; read by the
// parent via getLastName()/getLastCode() after receiving that event - safe without a lock for
// the same reason Context::result is (see AlertDialog.cpp).
std::string lastName;
std::string lastCode;

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, tr("Select Time zone"));
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    // Margins around the view's card
    auto* wrapper = lv_obj_create(parent);
    lv_obj_remove_style_all(wrapper);
    lv_obj_set_width(wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(wrapper, 1);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(wrapper, LV_DPX(8), LV_STATE_DEFAULT);

    ctx->view->createWidgets(wrapper);
}

void destroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->view->destroyWidgets();
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    // argv layout: [0]="1"/"0" (saveTimeZone).

    Context ctx;
    ctx.appInstanceId = appInstanceId;
    ctx.saveTimeZone = argc > 0 && argv[0][0] == '1';
    // Picking a time zone, or detecting one, closes the app with it
    ctx.view = std::make_unique<TimeZoneView>([&ctx](const std::string& name, const std::string& code) {
        if (ctx.saveTimeZone) {
            settings::setTimeZone(name, code);
        }
        lastName = name;
        lastCode = code;
        ctx.result = 0; // Ok
        app_event_emit_close(ctx.appInstanceId);
    });

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    WindowId window = window_manager_create_ext(appInstanceId, createWidgets, destroyWidgets, &ctx);

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
    ctx.view.reset();
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);

    return ctx.result;
}

} // namespace

uint32_t start(uint32_t callerAppInstanceId, bool saveTimeZone) {
    const char* argv[] = { saveTimeZone ? "1" : "0" };
    uint32_t instanceId = 0;
    AppStartContext context = app_start_context_for_manifest(&manifest);
    app_start_context_set_arguments_ext(&context, 1, argv);
    app_start_context_set_parent(&context, callerAppInstanceId);
    app_start_with_context(&context, &instanceId);
    return instanceId;
}

std::string getLastName() {
    return lastName;
}

std::string getLastCode() {
    return lastCode;
}

extern const ::AppManifest manifest = {
    .id = "tactility.timezone",
    .name = "Select Time zone",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = {}
};

}
