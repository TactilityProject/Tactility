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

class PowerApp : public App {

    Timer update_timer = Timer(Timer::Type::Periodic, kernel::millisToTicks(1000),[]() { onTimer(); });

    ::Device* power = nullptr;

    lv_obj_t* enableLabel = nullptr;
    lv_obj_t* enableSwitch = nullptr;
    lv_obj_t* quickChargeLabel = nullptr;
    lv_obj_t* quickChargeSwitch = nullptr;
    lv_obj_t* batteryVoltageLabel = nullptr;
    lv_obj_t* chargeStateLabel = nullptr;
    lv_obj_t* chargeLevelLabel = nullptr;
    lv_obj_t* currentLabel = nullptr;

    static void onTimer() {
        auto app = optApp();
        if (app != nullptr) {
            app->updateUi();
        }
    }

    void onPowerEnabledChanged(lv_event_t* event) {
        lv_event_code_t code = lv_event_get_code(event);
        auto* enable_switch = static_cast<lv_obj_t*>(lv_event_get_target(event));
        if (code == LV_EVENT_VALUE_CHANGED) {
            bool is_on = lv_obj_has_state(enable_switch, LV_STATE_CHECKED);

            if (power_supply_is_allowed_to_charge(power) != is_on) {
                power_supply_set_allowed_to_charge(power, is_on);
                updateUi();
            }
        }
    }

    static void onPowerEnabledChangedCallback(lv_event_t* event) {
        auto* app = (PowerApp*)lv_event_get_user_data(event);
        app->onPowerEnabledChanged(event);
    }

    void onQuickChargeChanged(lv_event_t* event) {
        lv_event_code_t code = lv_event_get_code(event);
        auto* qc_switch = static_cast<lv_obj_t*>(lv_event_get_target(event));
        if (code == LV_EVENT_VALUE_CHANGED) {
            bool is_on = lv_obj_has_state(qc_switch, LV_STATE_CHECKED);

            if (power_supply_is_quick_charge_enabled(power) != is_on) {
                power_supply_set_quick_charge_enabled(power, is_on);
                updateUi();
            }
        }
    }

    static void onQuickChargeChangedCallback(lv_event_t* event) {
        auto* app = (PowerApp*)lv_event_get_user_data(event);
        app->onQuickChargeChanged(event);
    }

    void updateUi() {
        if (chargeStateLabel == nullptr) {
            return;
        }

        const char* charge_state;
        PowerSupplyPropertyValue property_value;
        if (power_supply_get_property(power, POWER_SUPPLY_PROP_IS_CHARGING, &property_value) == ERROR_NONE) {
            charge_state = property_value.int_value ? "yes" : "no";
        } else {
            charge_state = "N/A";
        }

        int charge_level;
        bool charge_level_scaled_set = false;
        if (power_supply_get_property(power, POWER_SUPPLY_PROP_CAPACITY, &property_value) == ERROR_NONE) {
            charge_level = property_value.int_value;
            charge_level_scaled_set = true;
        }

        bool charging_enabled_set = power_supply_supports_charge_control(power);
        bool charging_enabled_and_allowed = charging_enabled_set && power_supply_is_allowed_to_charge(power);

        bool quick_charge_set = power_supply_supports_quick_charge(power);
        bool quick_charge_enabled = quick_charge_set && power_supply_is_quick_charge_enabled(power);

        int current;
        bool current_set = false;
        if (power_supply_get_property(power, POWER_SUPPLY_PROP_CURRENT, &property_value) == ERROR_NONE) {
            current = property_value.int_value;
            current_set = true;
        }

        int battery_voltage;
        bool battery_voltage_set = false;
        if (power_supply_get_property(power, POWER_SUPPLY_PROP_VOLTAGE, &property_value) == ERROR_NONE) {
            battery_voltage = property_value.int_value;
            battery_voltage_set = true;
        }

        lvgl::lock(kernel::millisToTicks(1000));

        if (charging_enabled_set) {
            lv_obj_set_state(enableSwitch, LV_STATE_CHECKED, charging_enabled_and_allowed);
            lv_obj_remove_flag(enableSwitch, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(enableLabel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(enableSwitch, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(enableLabel, LV_OBJ_FLAG_HIDDEN);
        }

        if (quick_charge_set) {
            lv_obj_set_state(quickChargeSwitch, LV_STATE_CHECKED, quick_charge_enabled);
            lv_obj_remove_flag(quickChargeSwitch, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(quickChargeLabel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(quickChargeSwitch, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(quickChargeLabel, LV_OBJ_FLAG_HIDDEN);
        }

        lv_label_set_text_fmt(chargeStateLabel, "Charging: %s", charge_state);

        if (battery_voltage_set) {
            lv_label_set_text_fmt(batteryVoltageLabel, "Battery voltage: %d mV", battery_voltage);
        } else {
            lv_label_set_text_fmt(batteryVoltageLabel, "Battery voltage: N/A");
        }

        if (charge_level_scaled_set) {
            lv_label_set_text_fmt(chargeLevelLabel, "Charge level: %d%%", charge_level);
        } else {
            lv_label_set_text_fmt(chargeLevelLabel, "Charge level: N/A");
        }

        if (current_set) {
            lv_label_set_text_fmt(currentLabel, "Current: %d mA", current);
        } else {
            lv_label_set_text_fmt(currentLabel, "Current: N/A");
        }

        lvgl::unlock();
    }

public:

    void onCreate(AppContext& app) override {
        power = device_find_first_active_by_type(&POWER_SUPPLY_TYPE);
    }

    void onShow(AppContext& app, lv_obj_t* parent) override {
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

        lvgl::toolbar_create(parent, app);

        if (power == nullptr) {
            return;
        }

        lv_obj_t* wrapper = lv_obj_create(parent);
        lv_obj_set_width(wrapper, LV_PCT(100));
        lv_obj_set_style_border_width(wrapper, 0, 0);
        lv_obj_set_flex_grow(wrapper, 1);
        lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);

        // Row: charge enable/disable
        lv_obj_t* switch_container = lv_obj_create(wrapper);
        lv_obj_set_width(switch_container, LV_PCT(100));
        lv_obj_set_height(switch_container, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(switch_container, 0, 0);
        lv_obj_set_style_pad_gap(switch_container, 0, 0);
        lvgl::obj_set_style_bg_invisible(switch_container);

        enableLabel = lv_label_create(switch_container);
        lv_label_set_text(enableLabel, "Charging enabled");
        lv_obj_set_align(enableLabel, LV_ALIGN_LEFT_MID);

        lv_obj_t* enable_switch = lv_switch_create(switch_container);
        lv_obj_add_event_cb(enable_switch, onPowerEnabledChangedCallback, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_set_align(enable_switch, LV_ALIGN_RIGHT_MID);
        enableSwitch = enable_switch;

        // Row: quick charge enable/disable
        lv_obj_t* qc_container = lv_obj_create(wrapper);
        lv_obj_set_width(qc_container, LV_PCT(100));
        lv_obj_set_height(qc_container, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(qc_container, 0, 0);
        lv_obj_set_style_pad_gap(qc_container, 0, 0);
        lvgl::obj_set_style_bg_invisible(qc_container);

        quickChargeLabel = lv_label_create(qc_container);
        lv_label_set_text(quickChargeLabel, "Quick charge");
        lv_obj_set_align(quickChargeLabel, LV_ALIGN_LEFT_MID);

        lv_obj_t* qc_switch = lv_switch_create(qc_container);
        lv_obj_add_event_cb(qc_switch, onQuickChargeChangedCallback, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_set_align(qc_switch, LV_ALIGN_RIGHT_MID);
        quickChargeSwitch = qc_switch;

        chargeStateLabel = lv_label_create(wrapper);
        chargeLevelLabel = lv_label_create(wrapper);
        batteryVoltageLabel = lv_label_create(wrapper);
        currentLabel = lv_label_create(wrapper);

        updateUi();

        update_timer.start();
    }

    void onHide(AppContext& app) override {
        update_timer.stop();
        enableLabel = nullptr;
        enableSwitch = nullptr;
        quickChargeLabel = nullptr;
        quickChargeSwitch = nullptr;
        chargeStateLabel = nullptr;
        chargeLevelLabel = nullptr;
        batteryVoltageLabel = nullptr;
        currentLabel = nullptr;
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
