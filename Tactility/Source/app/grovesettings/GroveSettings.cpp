#include <vector>

#include <lvgl.h>

#include <tactility/device.h>
#include <tactility/drivers/grove.h>
#include <tactility/lvgl_icon_shared.h>

#include <Tactility/Tactility.h>
#include <Tactility/lvgl/Toolbar.h>

namespace tt::app::grovesettings {

class GroveSettingsApp final : public App {

    std::vector<::Device*> devices;

    void collectDevices() {
        devices.clear();
        device_for_each_of_type(&GROVE_TYPE, &devices, [](auto* device, auto* context) {
            auto* vec = static_cast<std::vector<::Device*>*>(context);
            vec->push_back(device);
            return true;
        });
    }

    static void onModeChanged(lv_event_t* e) {
        auto* device = static_cast<::Device*>(lv_event_get_user_data(e));
        auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(e));
        auto mode = static_cast<GroveMode>(lv_dropdown_get_selected(dropdown));
        grove_set_mode(device, mode);
    }

public:
    void onShow(AppContext& app, lv_obj_t* parent) override {
        collectDevices();

        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

        lvgl::toolbar_create(parent, app);

        auto* main_wrapper = lv_obj_create(parent);
        lv_obj_set_flex_flow(main_wrapper, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_width(main_wrapper, LV_PCT(100));
        lv_obj_set_flex_grow(main_wrapper, 1);

        for (auto* device : devices) {
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
};

extern const AppManifest manifest = {
    .appId = "GroveSettings",
    .appName = "Grove",
    .appIcon = LVGL_ICON_SHARED_CABLE,
    .appCategory = Category::Settings,
    .createApp = create<GroveSettingsApp>
};

}
