#include <Tactility/hal/usb/Usb.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

#include <lvgl.h>
#include <lvgl/widgets/toolbar.h>

#define TAG "usb_settings"

namespace tt::app::usbsettings {

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;
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

void onRebootMassStorageSdmmc(lv_event_t* event) {
    hal::usb::rebootIntoMassStorageSdmmc();
}

// Flash reboot handler
void onRebootMassStorageFlash(lv_event_t* event) {
    hal::usb::rebootIntoMassStorageFlash();
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    auto* toolbar = lvgl_toolbar_create(parent, "USB");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);
    lv_obj_align(toolbar, LV_ALIGN_TOP_MID, 0, 0);

    // Create a wrapper container for buttons
    auto* wrapper = lv_obj_create(parent);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(wrapper, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(wrapper, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_align(wrapper, LV_ALIGN_CENTER, 0, 0);

    bool hasSd = hal::usb::canRebootIntoMassStorageSdmmc();
    bool hasFlash = hal::usb::canRebootIntoMassStorageFlash();

    if (hasSd) {
        auto* button_sd = lv_button_create(wrapper);
        auto* label_sd = lv_label_create(button_sd);
        lv_label_set_text(label_sd, "Reboot as USB storage (SD)");
        lv_obj_add_event_cb(button_sd, onRebootMassStorageSdmmc, LV_EVENT_SHORT_CLICKED, nullptr);
    }

    if (hasFlash) {
        auto* button_flash = lv_button_create(wrapper);
        auto* label_flash = lv_label_create(button_flash);
        lv_label_set_text(label_flash, "Reboot as USB storage (Flash)");
        lv_obj_add_event_cb(button_flash, onRebootMassStorageFlash, LV_EVENT_SHORT_CLICKED, nullptr);
    }

    if (!hasSd && !hasFlash) {
        bool supported = hal::usb::isSupported();
        const char* message = supported ? "USB storage not available" : "USB driver not supported";
        auto* label = lv_label_create(wrapper);
        lv_label_set_text(label, message);
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
    .id = "UsbSettings",
    .name = "USB",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) }
};

} // namespace
