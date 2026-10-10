#include <Tactility/hal/usb/Usb.h>
#include <Tactility/hal/usb/UsbHost.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>

#include <lvgl.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/toolbar.h>

#include <atomic>

#define TAG "usb_settings"

namespace tt::app::usbsettings {

extern const ::AppManifest manifest;

namespace {

enum class Request { None, Disable, Enable };

struct Context {
    uint32_t appInstanceId;
    TaskEventGroup* eventGroup = nullptr;
    uint32_t requestBit = 0;
    // Set by the switch and applied by appMain: stopping the host runs device listeners that take the LVGL lock
    std::atomic<Request> hostRequest { Request::None };

    // Read by appMain outside the LVGL lock, used by the widgets with the LVGL lock held
    bool hostEnabled = false;

    // nullptr while the widgets don't exist
    lv_obj_t* hostSwitch = nullptr;
};


void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void onRebootMassStorageSdmmc(lv_event_t* event) {
    hal::usb::rebootIntoMassStorageSdmmc();
}

// Flash reboot handler
void onRebootMassStorageFlash(lv_event_t* event) {
    hal::usb::rebootIntoMassStorageFlash();
}

void onHostSwitch(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* sw = static_cast<lv_obj_t*>(lv_event_get_target(event));
    ctx->hostRequest = lv_obj_has_state(sw, LV_STATE_CHECKED) ? Request::Enable : Request::Disable;
    task_event_group_signal(ctx->eventGroup, ctx->requestBit);
}

void onHostSwitchDeleted(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    ctx->hostSwitch = nullptr;
}

// Must be called with the LVGL lock held
void refresh(Context* ctx) {
    if (ctx->hostSwitch != nullptr) {
        lv_obj_set_state(ctx->hostSwitch, LV_STATE_CHECKED, ctx->hostEnabled);
    }
}

// A card with the label left and the control right
lv_obj_t* createRow(lv_obj_t* parent, const char* text) {
    auto* row = lvgl_card_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(row, false);

    auto* label = lv_label_create(row);
    lv_label_set_text(label, text);
    lv_obj_set_flex_grow(label, 1);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    return row;
}

void createRebootRow(lv_obj_t* parent, const char* text, lv_event_cb_t onReboot) {
    auto* row = createRow(parent, text);
    auto* button = lv_button_create(row);
    auto* label = lv_label_create(button);
    lv_label_set_text(label, "Reboot");
    lv_obj_add_event_cb(button, onReboot, LV_EVENT_SHORT_CLICKED, nullptr);
}

void createUsbHostRow(lv_obj_t* parent, Context* ctx) {
    auto* row = createRow(parent, "USB host");
    ctx->hostSwitch = lv_switch_create(row);
    lv_obj_add_event_cb(ctx->hostSwitch, onHostSwitch, LV_EVENT_VALUE_CHANGED, ctx);
    // Removed with the widgets, which happens when another window covers this one
    lv_obj_add_event_cb(ctx->hostSwitch, onHostSwitchDeleted, LV_EVENT_DELETE, ctx);
    refresh(ctx);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "USB");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    auto* main_wrapper = lv_obj_create(parent);
    lv_obj_set_style_border_width(main_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(main_wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(main_wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(main_wrapper, 1);

    const bool hasSd = hal::usb::canRebootIntoMassStorageSdmmc();
    const bool hasFlash = hal::usb::canRebootIntoMassStorageFlash();
    const bool hasHost = hal::usbhost::isAvailable();

    // The storage is only named when there is a choice
    if (hasSd) {
        createRebootRow(main_wrapper, hasFlash ? "USB drive mode (SD card)" : "USB drive mode", onRebootMassStorageSdmmc);
    }
    if (hasFlash) {
        createRebootRow(main_wrapper, hasSd ? "USB drive mode (internal)" : "USB drive mode", onRebootMassStorageFlash);
    }

    if (hasHost) {
        createUsbHostRow(main_wrapper, ctx);
    }

    if (!hasSd && !hasFlash && !hasHost) {
        bool supported = hal::usb::isSupported();
        const char* message = supported ? "USB storage not available" : "USB driver not supported";
        auto* label = lv_label_create(main_wrapper);
        lv_label_set_text(label, message);
    }
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    Context ctx {};
    ctx.appInstanceId = appInstanceId;

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);
    ctx.eventGroup = &event_group;
    check(task_event_group_claim_bit(&event_group, &ctx.requestBit) == ERROR_NONE);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    const bool hasHost = hal::usbhost::isAvailable();
    if (hasHost) {
        ctx.hostEnabled = hal::usbhost::isEnabled();
    }

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);

    bool shouldClose = false;
    while (!shouldClose) {
        task_event_group_wait_any(&event_group, nullptr, portMAX_DELAY);

        const auto hostRequest = ctx.hostRequest.exchange(Request::None);
        if (hostRequest != Request::None) {
            hal::usbhost::setEnabled(hostRequest == Request::Enable);
            const bool hostEnabled = hal::usbhost::isEnabled();
            lvgl_lock();
            ctx.hostEnabled = hostEnabled;
            refresh(&ctx);
            lvgl_unlock();
        }

        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            switch (event.type) {
                case APP_EVENT_CLOSE:
                    shouldClose = true;
                    break;
                default:
                    break;
            }
            if (shouldClose) break;
        }
    }

    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_release_bit(&event_group, ctx.requestBit);
    task_event_group_destruct(&event_group);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "tactility.usbsettings",
    .name = "USB",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {}
};

} // namespace
