#include <Tactility/app/AppContext.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/lvgl/Style.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/service/loader/Loader.h>

#include <Tactility/Timer.h>

#include <tactility/device.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/lvgl_icon_shared.h>

#include <lvgl.h>

#include <vector>

namespace tt::app::power {

#define TAG "power"

extern const AppManifest manifest;

class PowerApp;

/** Returns the app data if the app is active. Note that this could clash if the same app is started twice and a background thread is slow. */
std::shared_ptr<PowerApp> optApp() {
    auto appContext = getCurrentAppContext();
    if (appContext != nullptr && appContext->getManifest().appId == manifest.appId) {
        return std::static_pointer_cast<PowerApp>(appContext->getApp());
    } else {
        return nullptr;
    }
}

namespace {
constexpr PowerSupplyProperty DISPLAYED_PROPERTIES[] = {
    POWER_SUPPLY_PROP_IS_CHARGING,
    POWER_SUPPLY_PROP_VOLTAGE,
    POWER_SUPPLY_PROP_CAPACITY,
    POWER_SUPPLY_PROP_CURRENT,
};
} // namespace

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

class PowerApp : public App {

    Timer update_timer = Timer(Timer::Type::Periodic, kernel::millisToTicks(1000),[]() { onTimer(); });

    std::vector<DeviceEntry> entries;

    static void onTimer() {
        auto app = optApp();
        if (app != nullptr) {
            app->updateUi();
        }
    }

    static bool collectDevice(::Device* device, void* context) {
        auto* devices = static_cast<std::vector<::Device*>*>(context);
        devices->push_back(device);
        return true;
    }

    void onPowerEnabledChanged(lv_event_t* event) {
        lv_event_code_t code = lv_event_get_code(event);
        auto* enable_switch = static_cast<lv_obj_t*>(lv_event_get_target(event));
        if (code == LV_EVENT_VALUE_CHANGED) {
            bool is_on = lv_obj_has_state(enable_switch, LV_STATE_CHECKED);
            auto* device = static_cast<::Device*>(lv_event_get_user_data(event));

            if (power_supply_is_allowed_to_charge(device) != is_on) {
                power_supply_set_allowed_to_charge(device, is_on);
                updateUi();
            }
        }
    }

    static void onPowerEnabledChangedCallback(lv_event_t* event) {
        auto app = optApp();
        if (app != nullptr) {
            app->onPowerEnabledChanged(event);
        }
    }

    void onQuickChargeChanged(lv_event_t* event) {
        lv_event_code_t code = lv_event_get_code(event);
        auto* qc_switch = static_cast<lv_obj_t*>(lv_event_get_target(event));
        if (code == LV_EVENT_VALUE_CHANGED) {
            bool is_on = lv_obj_has_state(qc_switch, LV_STATE_CHECKED);
            auto* device = static_cast<::Device*>(lv_event_get_user_data(event));

            if (power_supply_is_quick_charge_enabled(device) != is_on) {
                power_supply_set_quick_charge_enabled(device, is_on);
                updateUi();
            }
        }
    }

    static void onQuickChargeChangedCallback(lv_event_t* event) {
        auto app = optApp();
        if (app != nullptr) {
            app->onQuickChargeChanged(event);
        }
    }

    static void setPropertyLabelText(lv_obj_t* label, PowerSupplyProperty property, const PowerSupplyPropertyValue& value) {
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

    void updateUi() {
        if (entries.empty()) {
            return;
        }

        lvgl::lock(kernel::millisToTicks(1000));

        for (auto& entry : entries) {
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

        lvgl::unlock();
    }

public:

    void onCreate(AppContext& app) override {}

    void onShow(AppContext& app, lv_obj_t* parent) override {
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

        lvgl::toolbar_create(parent, app);

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

        entries.clear();
        entries.reserve(devices.size());

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
                lv_obj_add_event_cb(enable_switch, onPowerEnabledChangedCallback, LV_EVENT_VALUE_CHANGED, device);
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
                lv_obj_add_event_cb(qc_switch, onQuickChargeChangedCallback, LV_EVENT_VALUE_CHANGED, device);
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

            entries.push_back(entry);
        }

        update_timer.start();
    }

    void onHide(AppContext& app) override {
        update_timer.stop();
        entries.clear();
    }
};

extern const AppManifest manifest = {
    .appId = "Power",
    .appName = "Power",
    .appIcon = LVGL_ICON_SHARED_ELECTRIC_BOLT,
    .appCategory = Category::Settings,
    .createApp = create<PowerApp>
};

} // namespace
