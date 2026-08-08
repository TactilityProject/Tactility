#include <vector>

#include <lvgl.h>

#include <tactility/device.h>
#include <tactility/drivers/grove.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

#include <lvgl/widgets/toolbar.h>

namespace tt::app::grovesettings {

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;
    std::vector<::Device*> devices;
};


void collectDevices(Context* ctx) {
    ctx->devices.clear();
    device_for_each_of_type(&GROVE_TYPE, &ctx->devices, [](auto* device, auto* context) {
        auto* vec = static_cast<std::vector<::Device*>*>(context);
        vec->push_back(device);
        return true;
    });
}

void onModeChanged(lv_event_t* e) {
    auto* device = static_cast<::Device*>(lv_event_get_user_data(e));
    auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(e));
    auto mode = static_cast<GroveMode>(lv_dropdown_get_selected(dropdown));
    grove_set_mode(device, mode);
}

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    // Async, non-blocking - must NOT call app_manager_stop() directly here: that bound-waits
    // (thread_join) for this app's own thread to finish, which needs the LVGL lock
    // (window_manager_remove()) - but this callback runs ON the LVGL task, which would
    // deadlock against itself.
    AppEvent closeEvent { .type = APP_EVENT_CLOSE, .timestamp = 0, .result = {} };
    app_event_emit(ctx->appInstanceId, &closeEvent);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    collectDevices(ctx);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "Grove");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    auto* main_wrapper = lv_obj_create(parent);
    lv_obj_set_flex_flow(main_wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(main_wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(main_wrapper, 1);

    for (auto* device : ctx->devices) {
        auto* row = lv_obj_create(main_wrapper);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(row, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(row, 0, LV_STATE_DEFAULT);

        auto* label = lv_label_create(row);
        lv_label_set_text(label, device->name);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

        auto* dropdown = lv_dropdown_create(row);
        lv_dropdown_set_options(dropdown, "Disabled\nUART\nI2C");
        lv_obj_align(dropdown, LV_ALIGN_RIGHT_MID, 0, 0);

        GroveMode current = GROVE_MODE_DISABLED;
        grove_get_mode(device, &current);
        lv_dropdown_set_selected(dropdown, static_cast<uint32_t>(current));

        lv_obj_add_event_cb(dropdown, onModeChanged, LV_EVENT_VALUE_CHANGED, device);
    }
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
            default:
                break;
        }
    }

    window_manager_remove(window);
    app_event_unsubscribe(&sub);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "GroveSettings",
    .name = "Grove",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) }
};

}
