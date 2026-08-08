#include <Tactility/lvgl/Style.h>
#include <Tactility/Timer.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/device.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/time.h>

#include <lvgl/lvgl.h>
#include <lvgl/widgets/toolbar.h>

#include <vector>

namespace tt::app::power {

#define TAG "power"

extern const ::AppManifest manifest;

namespace {

constexpr PowerSupplyProperty DISPLAYED_PROPERTIES[] = {
    POWER_SUPPLY_PROP_IS_CHARGING,
    POWER_SUPPLY_PROP_VOLTAGE,
    POWER_SUPPLY_PROP_CAPACITY,
    POWER_SUPPLY_PROP_CURRENT,
};

struct PropertyWidget {
    PowerSupplyProperty property;
    lv_obj_t* label;
};

struct DeviceEntry {
    ::Device* device = nullptr;
    lv_obj_t* enableSwitch = nullptr;
    lv_obj_t* quickChargeSwitch = nullptr;
    std::vector<PropertyWidget> propertyWidgets;
};

struct Context {
    uint32_t appInstanceId;
    std::unique_ptr<Timer> timer;
    std::vector<DeviceEntry> entries;
};


bool collectDevice(::Device* device, void* context) {
    auto* devices = static_cast<std::vector<::Device*>*>(context);
    devices->push_back(device);
    return true;
}

void setPropertyLabelText(lv_obj_t* label, PowerSupplyProperty property, const PowerSupplyPropertyValue& value) {
    switch (property) {
        case POWER_SUPPLY_PROP_IS_CHARGING:
            lv_label_set_text_fmt(label, "Charging: %s", value.int_value ? "yes" : "no");
            break;
        case POWER_SUPPLY_PROP_VOLTAGE:
            lv_label_set_text_fmt(label, "Battery voltage: %d mV", value.int_value);
            break;
        case POWER_SUPPLY_PROP_CAPACITY:
            lv_label_set_text_fmt(label, "Charge level: %d%%", value.int_value);
            break;
        case POWER_SUPPLY_PROP_CURRENT:
            lv_label_set_text_fmt(label, "Current: %d mA", value.int_value);
            break;
    }
}

void updateUi(Context* ctx) {
    if (ctx->entries.empty()) {
        return;
    }

    lvgl_lock();

    for (auto& entry : ctx->entries) {
        if (entry.enableSwitch != nullptr) {
            lv_obj_set_state(entry.enableSwitch, LV_STATE_CHECKED, power_supply_is_allowed_to_charge(entry.device));
        }

        if (entry.quickChargeSwitch != nullptr) {
            lv_obj_set_state(entry.quickChargeSwitch, LV_STATE_CHECKED, power_supply_is_quick_charge_enabled(entry.device));
        }

        PowerSupplyPropertyValue value;
        for (auto& widget : entry.propertyWidgets) {
            if (power_supply_get_property(entry.device, widget.property, &value) == ERROR_NONE) {
                setPropertyLabelText(widget.label, widget.property, value);
            }
        }
    }

    lvgl_unlock();
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

void onPowerEnabledChanged(lv_event_t* event) {
    lv_event_code_t code = lv_event_get_code(event);
    auto* enable_switch = lv_event_get_target_obj(event);
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* device = static_cast<::Device*>(lv_obj_get_user_data(enable_switch));
    if (code == LV_EVENT_VALUE_CHANGED) {
        bool is_on = lv_obj_has_state(enable_switch, LV_STATE_CHECKED);

        if (power_supply_is_allowed_to_charge(device) != is_on) {
            power_supply_set_allowed_to_charge(device, is_on);
            updateUi(ctx);
        }
    }
}

void onQuickChargeChanged(lv_event_t* event) {
    lv_event_code_t code = lv_event_get_code(event);
    auto* qc_switch = lv_event_get_target_obj(event);
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* device = static_cast<::Device*>(lv_obj_get_user_data(qc_switch));
    if (code == LV_EVENT_VALUE_CHANGED) {
        bool is_on = lv_obj_has_state(qc_switch, LV_STATE_CHECKED);

        if (power_supply_is_quick_charge_enabled(device) != is_on) {
            power_supply_set_quick_charge_enabled(device, is_on);
            updateUi(ctx);
        }
    }
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "Power");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    std::vector<::Device*> devices;
    device_for_each_of_type(&POWER_SUPPLY_TYPE, &devices, collectDevice);

    if (devices.empty()) {
        return;
    }

    lv_obj_t* wrapper = lv_obj_create(parent);
    lv_obj_set_width(wrapper, LV_PCT(100));
    lv_obj_set_style_border_width(wrapper, 0, 0);
    lv_obj_set_flex_grow(wrapper, 1);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);

    ctx->entries.clear();
    ctx->entries.reserve(devices.size());

    for (size_t i = 0; i < devices.size(); i++) {
        ::Device* device = devices[i];

        DeviceEntry entry;
        entry.device = device;

        lv_obj_t* header = lv_label_create(wrapper);
        lv_label_set_text_fmt(header, "%s:", device->name);

        if (power_supply_supports_charge_control(device)) {
            lv_obj_t* switch_container = lv_obj_create(wrapper);
            lv_obj_set_width(switch_container, LV_PCT(100));
            lv_obj_set_height(switch_container, LV_SIZE_CONTENT);
            lv_obj_set_style_pad_all(switch_container, 0, 0);
            lv_obj_set_style_pad_gap(switch_container, 0, 0);
            lvgl::obj_set_style_bg_invisible(switch_container);

            lv_obj_t* label = lv_label_create(switch_container);
            lv_label_set_text(label, "Charging enabled");
            lv_obj_set_align(label, LV_ALIGN_LEFT_MID);

            lv_obj_t* enable_switch = lv_switch_create(switch_container);
            lv_obj_set_user_data(enable_switch, device);
            lv_obj_add_event_cb(enable_switch, onPowerEnabledChanged, LV_EVENT_VALUE_CHANGED, ctx);
            lv_obj_set_align(enable_switch, LV_ALIGN_RIGHT_MID);
            lv_obj_set_state(enable_switch, LV_STATE_CHECKED, power_supply_is_allowed_to_charge(device));
            entry.enableSwitch = enable_switch;
        }

        if (power_supply_supports_quick_charge(device)) {
            lv_obj_t* qc_container = lv_obj_create(wrapper);
            lv_obj_set_width(qc_container, LV_PCT(100));
            lv_obj_set_height(qc_container, LV_SIZE_CONTENT);
            lv_obj_set_style_pad_all(qc_container, 0, 0);
            lv_obj_set_style_pad_gap(qc_container, 0, 0);
            lvgl::obj_set_style_bg_invisible(qc_container);

            lv_obj_t* label = lv_label_create(qc_container);
            lv_label_set_text(label, "Quick charge");
            lv_obj_set_align(label, LV_ALIGN_LEFT_MID);

            lv_obj_t* qc_switch = lv_switch_create(qc_container);
            lv_obj_set_user_data(qc_switch, device);
            lv_obj_add_event_cb(qc_switch, onQuickChargeChanged, LV_EVENT_VALUE_CHANGED, ctx);
            lv_obj_set_align(qc_switch, LV_ALIGN_RIGHT_MID);
            lv_obj_set_state(qc_switch, LV_STATE_CHECKED, power_supply_is_quick_charge_enabled(device));
            entry.quickChargeSwitch = qc_switch;
        }

        PowerSupplyPropertyValue value;
        for (auto property : DISPLAYED_PROPERTIES) {
            if (power_supply_get_property(device, property, &value) == ERROR_NONE) {
                lv_obj_t* label = lv_label_create(wrapper);
                lv_obj_set_style_margin_left(label, 24, LV_STATE_DEFAULT);
                setPropertyLabelText(label, property, value);
                entry.propertyWidgets.push_back({ property, label });
            }
        }

        ctx->entries.push_back(entry);
    }
}

int32_t appMain(uint32_t appInstanceId, int argc, char* argv[]) {
    Context ctx {};
    ctx.appInstanceId = appInstanceId;

    // Runs for this app instance's whole lifetime, mirroring GpsSettings/SystemInfo - there's no
    // push notification for power-supply property changes, so this is the only way this screen
    // finds out about them.
    ctx.timer = std::make_unique<Timer>(Timer::Type::Periodic, millis_to_ticks(1000), [&ctx] {
        updateUi(&ctx);
    });

    AppEventSubscription sub {};
    sub.app_instance_id = appInstanceId;
    app_event_subscribe(&sub);

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);
    ctx.timer->start();

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

    ctx.timer->stop();
    window_manager_remove(window);
    app_event_unsubscribe(&sub);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "Power",
    .name = "Power",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) }
};

} // namespace
