#include "Tactility/Tactility.h"
#include "tactility/drivers/display.h"


#include <Tactility/app/AppContext.h>
#include <Tactility/app/AppRegistration.h>
#include <Tactility/hal/display/DisplayDevice.h>
#include <Tactility/service/loader/Loader.h>

#include <lvgl.h>
#include <tactility/device.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/hal/Device.h>
#include <tactility/lvgl_fonts.h>
#include <tactility/lvgl_icon_shared.h>

namespace tt::app::poweroff {

extern const AppManifest manifest;

class PowerOffApp final : public App {

    static void showPoweredOffScreen() {
        auto* screen = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
        lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        auto* title = lv_label_create(screen);
        lv_label_set_text(title, "Tactility");
        lv_obj_set_style_text_font(title, lvgl_get_text_font(FONT_SIZE_LARGE), 0);
        lv_obj_set_style_text_color(title, lv_color_black(), 0);

        auto* subtitle = lv_label_create(screen);
        lv_label_set_text(subtitle, "Powered off");
        lv_obj_set_style_text_color(subtitle, lv_color_black(), 0);

        lv_screen_load(screen);
    }

    static bool anyDeviceSupportsPowerOff() {
        bool any_supported = false;
        device_for_each_of_type(&POWER_SUPPLY_TYPE, &any_supported, [](Device* device, void* context) {
            if (device_is_ready(device) && power_supply_supports_power_off(device)) {
                *static_cast<bool*>(context) = true;
                return false;
            }
            return true;
        });
        return any_supported;
    }

    static void onYesPressed(lv_event_t* /*event*/) {
        if (!anyDeviceSupportsPowerOff()) {
            return;
        }

        Device* display;
        error_t error = device_get_first_by_type(&DISPLAY_TYPE, &display);
        // TODO: remove this logic path when all displays have been migrated to kernel display drivers
        if (error != ERROR_NONE) {
            // No display, power off now
            device_for_each_of_type(&POWER_SUPPLY_TYPE, nullptr, [](Device* device, void* /*context*/) {
                if (device_is_ready(device) && power_supply_supports_power_off(device)) {
                    power_supply_power_off(device);
                }
                return true;
            });
            return;
        }

        bool is_slow_refresh = display_has_capability(display, DISPLAY_CAPABILITY_SLOW_REFRESH);
        if (is_slow_refresh) {
            auto* lvgl_display = lv_display_get_default();
            showPoweredOffScreen();
            if (lvgl_display != nullptr) {
                lv_refr_now(lvgl_display);
            }
        }

        getMainDispatcher().dispatch([is_slow_refresh] {
            // Not necessary for LilyGO Paper S3, but other drivers with async rendering might need us to wait a bit.
            if (is_slow_refresh) {
                vTaskDelay(pdMS_TO_TICKS(2000));
            }
            device_for_each_of_type(&POWER_SUPPLY_TYPE, nullptr, [](Device* device, void* /*context*/) {
                if (device_is_ready(device) && power_supply_supports_power_off(device)) {
                    power_supply_power_off(device);
                }
                return true;
            });
        });
    }

    static void onNoPressed(lv_event_t* /*event*/) {
        stop(manifest.appId);
    }

public:

    void onShow(AppContext&, lv_obj_t* parent) override {
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        auto* label = lv_label_create(parent);
        lv_label_set_text(label, "Power off?");
        lv_obj_set_style_text_font(label, lvgl_get_text_font(FONT_SIZE_LARGE), 0);

        auto* button_wrapper = lv_obj_create(parent);
        lv_obj_set_flex_flow(button_wrapper, LV_FLEX_FLOW_ROW);
        lv_obj_set_size(button_wrapper, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_border_width(button_wrapper, 0, 0);
        lv_obj_set_flex_align(button_wrapper, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        auto* yes_button = lv_button_create(button_wrapper);
        auto* yes_label = lv_label_create(yes_button);
        lv_label_set_text(yes_label, "Yes");
        lv_obj_add_event_cb(yes_button, onYesPressed, LV_EVENT_SHORT_CLICKED, nullptr);

        auto* no_button = lv_button_create(button_wrapper);
        auto* no_label = lv_label_create(no_button);
        lv_label_set_text(no_label, "No");
        lv_obj_add_event_cb(no_button, onNoPressed, LV_EVENT_SHORT_CLICKED, nullptr);
    }
};

extern const AppManifest manifest = {
    .appId = "PowerOff",
    .appName = "Power Off",
    .appIcon = LVGL_ICON_SHARED_POWER_SETTINGS_NEW,
    .appCategory = Category::Settings,
    .appFlags = AppManifest::Flags::HideStatusBar,
    .createApp = create<PowerOffApp>
};

} // namespace
