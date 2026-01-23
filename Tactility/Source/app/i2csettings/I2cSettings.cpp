#include "Tactility/lvgl/Style.h"
#include "Tactility/lvgl/Toolbar.h"

#include <Tactility/Assets.h>
#include <Tactility/hal/i2c/I2c.h>
#include <Tactility/Tactility.h>

#include <lvgl.h>

namespace tt::app::i2csettings {

static void onSwitchToggled(lv_event_t* event) {
    lv_event_code_t code = lv_event_get_code(event);
    auto* state_switch = static_cast<lv_obj_t*>(lv_event_get_target(event));
    const hal::i2c::Configuration* configuration = static_cast<hal::i2c::Configuration*>(lv_event_get_user_data(event));
    if (code == LV_EVENT_VALUE_CHANGED) {
        bool should_enable = lv_obj_get_state(state_switch) & LV_STATE_CHECKED;
        bool is_enabled = hal::i2c::isStarted(configuration->port);
        if (is_enabled && !should_enable) {
            if (!hal::i2c::stop(configuration->port)) {
                lv_obj_add_state(state_switch, LV_STATE_CHECKED);
            }
        } else if (!is_enabled && should_enable) {
            if (!hal::i2c::start(configuration->port)) {
                lv_obj_remove_state(state_switch, LV_STATE_CHECKED);
            }
        }
    }
}

static void show(lv_obj_t* parent, const hal::i2c::Configuration& configuration) {
    auto* card = lv_obj_create(parent);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_margin_hor(card, 16, 0);
    lv_obj_set_style_margin_bottom(card, 16, 0);
    lv_obj_set_width(card, LV_PCT(100));
    auto* port_label = lv_label_create(card);
    const char* name = configuration.name.empty() ? "Unnamed" : configuration.name.c_str();
    lv_label_set_text_fmt(port_label, "%s (port %d)", name, configuration.port);

    // On-off switch

    if (configuration.isMutable) {
        auto* state_switch = lv_switch_create(card);
        lv_obj_align(state_switch, LV_ALIGN_TOP_RIGHT, 0, 0);

        if (hal::i2c::isStarted(configuration.port)) {
            lv_obj_add_state(state_switch, LV_STATE_CHECKED);
        }

        lv_obj_add_event_cb(state_switch, onSwitchToggled, LV_EVENT_VALUE_CHANGED, (void*) &configuration);
    }

    // SDA label
    auto* sda_pin_label = lv_label_create(card);
    const char* sda_pullup_text = configuration.config.sda_pullup_en ? "yes" : "no";
    lv_label_set_text_fmt(sda_pin_label, "SDA: pin %d, pullup: %s", configuration.config.sda_io_num, sda_pullup_text );
    lv_obj_align_to(sda_pin_label, port_label, LV_ALIGN_OUT_BOTTOM_LEFT, 16, 8);

    // SCL label
    auto* scl_pin_label = lv_label_create(card);
    const char* scl_pullup_text = configuration.config.scl_pullup_en ? "yes" : "no";
    lv_label_set_text_fmt(scl_pin_label, "SCL: pin %d, pullup: %s", configuration.config.scl_io_num, scl_pullup_text);
    lv_obj_align_to(scl_pin_label, sda_pin_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0);

    // Frequency:
    if (configuration.config.mode == I2C_MODE_MASTER) {
        auto* frequency_label = lv_label_create(card);
        lv_label_set_text_fmt(frequency_label, "Frequency: %" PRIu32 " Hz", configuration.config.master.clk_speed);
        lv_obj_align_to(frequency_label, scl_pin_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0);
    }
}

class I2cSettingsApp : public App {

    void onShow(AppContext& app, lv_obj_t* parent) override {
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);
        lvgl::toolbar_create(parent, app);

        auto* wrapper = lv_obj_create(parent);
        lv_obj_set_width(wrapper, LV_PCT(100));
        lv_obj_set_flex_grow(wrapper, 1);
        lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(wrapper, LV_STATE_DEFAULT, 0);
        lv_obj_set_style_pad_gap(wrapper, LV_STATE_DEFAULT, 0);
        lvgl::obj_set_style_bg_invisible(wrapper);

        for (const auto& configuration: getConfiguration()->hardware->i2c) {
            show(wrapper, configuration);
        }
    }
};

extern const AppManifest manifest = {
    .appId = "I2cSettings",
    .appName = "I2C",
    .appIcon = TT_ASSETS_APP_ICON_I2C_SETTINGS,
    .appCategory = Category::Settings,
    .createApp = create<I2cSettingsApp>
};

} // namespace
