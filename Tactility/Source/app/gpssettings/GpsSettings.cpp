#include <lvgl/icons/shared.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/toolbar.h>

#include <Tactility/Tactility.h>
#include <Tactility/Timer.h>
#include <Tactility/app/alertdialog/AlertDialog.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/time.h>

#include <atomic>
#include <cstring>
#include <lvgl.h>
#include <string>
#include <vector>

#include <gps/gps.h>
#include <gps/gps_settings.h>

namespace tt::app::addgps {
extern const ::AppManifest manifest;
}

namespace tt::app::gpssettings {

extern const ::AppManifest manifest;

namespace {

struct DeviceRow {
    Device* device;
    lv_obj_t* button;
    lv_obj_t* buttonLabel;
    bool hasConfiguration = false;
    size_t configurationIndex = 0;
};

struct Context {
    uint32_t appInstanceId;
    lv_obj_t* deviceListWrapper = nullptr;
    std::vector<DeviceRow> deviceRows;
    std::unique_ptr<Timer> timer;

    // Set when a delete confirmation is pending; read/cleared on this app's own thread when
    // the dialog's result arrives.
    bool hasPendingDelete = false;
    Device* pendingDeleteDevice = nullptr;
    size_t pendingDeleteIndex = 0;
};


void rebuildDeviceList(Context* ctx);
void updateDeviceStates(Context* ctx);
void createWidgets(lv_obj_t* parent, void* userData);

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void onAddGpsPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    // Fire-and-forget top-level launch, matching the original (its result never fed back into
    // this app; rebuildDeviceList() runs fresh whenever this app is resumed regardless).
    (void)ctx;
    uint32_t instanceId = 0;
    app_start(addgps::manifest.id, 0, nullptr, &instanceId);
}

void onDeviceButtonPressed(lv_event_t* event) {
    auto* button = lv_event_get_target_obj(event);
    auto* device = static_cast<Device*>(lv_obj_get_user_data(button));

    bool running = device_is_ready(device);
    // device_start()/device_stop() are potentially blocking calls, so use a dispatcher to not block the UI
    getMainDispatcher().dispatch([device, running] {
        if (running) {
            device_stop(device);
        } else {
            device_start(device);
        }
    });
}

// Finds the persisted configuration backing `device` (matched by its parent UART's name)
// and returns its index into gps_settings_for_each_configuration()'s ordering - the handle
// gps_settings_remove_configuration_at() needs to delete exactly this entry, even if another
// entry happens to have identical field values.
// Devicetree-declared GPS_TYPE devices have no such configuration and never match.
bool findConfigurationIndexForDevice(Device* device, size_t& outIndex) {
    auto* parent = device_get_parent(device);
    if (parent == nullptr) {
        return false;
    }

    struct FindContext {
        const char* uartName;
        size_t* outIndex;
        bool found;
    } findContext = { parent->name, &outIndex, false };

    gps_settings_for_each_configuration(&findContext, [](const GpsConfiguration* configuration, size_t index, void* untyped_context) {
        auto* ctx = static_cast<FindContext*>(untyped_context);
        if (!ctx->found && strcmp(configuration->uart_name, ctx->uartName) == 0) {
            *ctx->outIndex = index;
            ctx->found = true;
        }
    });

    return findContext.found;
}

void onDeleteButtonPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* button = lv_event_get_target_obj(event);
    auto* device = static_cast<Device*>(lv_obj_get_user_data(button));

    for (auto& row : ctx->deviceRows) {
        if (row.device == device && row.hasConfiguration) {
            ctx->pendingDeleteDevice = device;
            ctx->pendingDeleteIndex = row.configurationIndex;
            ctx->hasPendingDelete = true;
            alertdialog::start(ctx->appInstanceId, "Confirmation", std::string("Do you want to delete ") + device->name + "?", std::vector<std::string> { "Yes", "No" });
            return;
        }
    }
}

void createDeviceRow(Context* ctx, Device* device) {
    auto* wrapper = lv_obj_create(ctx->deviceListWrapper);
    lv_obj_set_size(wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wrapper, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_width(wrapper, 0, 0);
    lv_obj_set_style_pad_all(wrapper, 0, 0);

    auto* name_label = lv_label_create(wrapper);
    char model_name[64];
    if (gps_get_model_name(device, model_name, sizeof(model_name)) == ERROR_NONE) {
        lv_label_set_text(name_label, model_name);
    } else {
        lv_label_set_text(name_label, device->name);
    }

    auto* actions_wrapper = lv_obj_create(wrapper);
    lv_obj_set_size(actions_wrapper, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(actions_wrapper, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_border_width(actions_wrapper, 0, 0);
    lv_obj_set_style_pad_all(actions_wrapper, 0, 0);
    lv_obj_set_style_pad_column(actions_wrapper, 4, 0);

    auto* button = lv_button_create(actions_wrapper);
    lv_obj_add_event_cb(button, onDeviceButtonPressed, LV_EVENT_SHORT_CLICKED, ctx);
    lv_obj_set_user_data(button, device);
    auto* button_label = lv_label_create(button);
    lv_label_set_text(button_label, "Start");

    DeviceRow row { .device = device, .button = button, .buttonLabel = button_label };

    // Only devices backed by a persisted configuration (not devicetree-declared ones) can be deleted.
    size_t configurationIndex;
    if ((device->flags & DEVICE_FLAG_DYNAMIC) && findConfigurationIndexForDevice(device, configurationIndex)) {
        auto* delete_button = lv_button_create(actions_wrapper);
        lv_obj_add_event_cb(delete_button, onDeleteButtonPressed, LV_EVENT_SHORT_CLICKED, ctx);
        lv_obj_set_user_data(delete_button, device);
        auto* delete_label = lv_label_create(delete_button);
        lv_label_set_text(delete_label, LVGL_ICON_SHARED_DELETE);

        row.hasConfiguration = true;
        row.configurationIndex = configurationIndex;
    }

    ctx->deviceRows.push_back(row);
}

// Rebuilds the device list. Only needs to run when the set of devices could've changed (on
// creation, and after returning from AddGps) - button state itself is refreshed by the timer.
void rebuildDeviceList(Context* ctx) {
    lv_obj_clean(ctx->deviceListWrapper);
    ctx->deviceRows.clear();

    device_for_each_of_type(&GPS_TYPE, ctx, [](Device* device, void* context) {
        createDeviceRow(static_cast<Context*>(context), device);
        return true;
    });
}

void updateDeviceStates(Context* ctx) {
    lvgl_lock();
    for (const auto& row : ctx->deviceRows) {
        const char* text = "Start";
        bool enabled = true;

        if (device_is_ready(row.device)) {
            switch (gps_get_state(row.device)) {
                case GPS_STATE_PENDING_ON:
                    text = "Starting...";
                    enabled = false;
                    break;
                case GPS_STATE_PENDING_OFF:
                    text = "Stopping...";
                    enabled = false;
                    break;
                default:
                    text = "Stop";
                    enabled = true;
                    break;
            }
        }
        lv_label_set_text(row.buttonLabel, text);
        if (enabled) {
            lv_obj_remove_state(row.button, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(row.button, LV_STATE_DISABLED);
        }
    }
    lvgl_unlock();
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    uint8_t margin = (lvgl_get_ui_density() == LVGL_UI_DENSITY_COMPACT) ? 2 : 8;

    auto* toolbar = lvgl_toolbar_create(parent, "GPS");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);
    lvgl_toolbar_add_text_button_action(toolbar, LV_SYMBOL_PLUS, onAddGpsPressed, ctx);
    lv_obj_set_style_margin_bottom(toolbar, margin, LV_STATE_DEFAULT);

    ctx->deviceListWrapper = lv_obj_create(parent);
    lv_obj_set_size(ctx->deviceListWrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ctx->deviceListWrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_grow(ctx->deviceListWrapper, 1);
    lv_obj_set_style_border_width(ctx->deviceListWrapper, 0, 0);
    lv_obj_set_style_pad_hor(ctx->deviceListWrapper, margin, 0);
    lv_obj_set_style_pad_top(ctx->deviceListWrapper, 0, 0);
    lv_obj_set_style_pad_bottom(ctx->deviceListWrapper, margin, 0);
    lv_obj_set_style_pad_row(ctx->deviceListWrapper, margin, 0);

    rebuildDeviceList(ctx);
    updateDeviceStates(ctx);
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    Context ctx {};
    ctx.appInstanceId = appInstanceId;

    // Runs for this app instance's whole lifetime - there's no push notification for GPS
    // device state changes, so this is the only way this screen finds out about them.
    ctx.timer = std::make_unique<Timer>(Timer::Type::Periodic, seconds_to_ticks(1), [&ctx] {
        updateDeviceStates(&ctx);
    });

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);
    ctx.timer->start();

    bool shouldClose = false;
    while (!shouldClose) {
        task_event_group_wait_any(&event_group, nullptr, portMAX_DELAY);

        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            switch (event.type) {
                case APP_EVENT_CLOSE:
                    shouldClose = true;
                    break;
                case APP_EVENT_RESULT:
                    if (ctx.hasPendingDelete) {
                        ctx.hasPendingDelete = false;
                        if (event.result.result == 0) { // 0 = Yes
                            lvgl_lock();
                            std::erase_if(ctx.deviceRows, [&ctx](const DeviceRow& row) {
                                return row.device == ctx.pendingDeleteDevice;
                            });
                            lvgl_unlock();

                            gps_settings_remove_configuration_at(ctx.pendingDeleteIndex);
                            ctx.pendingDeleteDevice = nullptr;

                            lvgl_lock();
                            rebuildDeviceList(&ctx);
                            lvgl_unlock();
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

    ctx.timer->stop();
    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "tactility.gpssettings",
    .name = "GPS",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {}
};

} // namespace
