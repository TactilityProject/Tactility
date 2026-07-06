#include <Tactility/app/AppContext.h>
#include <Tactility/app/AppRegistration.h>
#include <Tactility/hal/display/DisplayDevice.h>
#include <Tactility/hal/power/PowerDevice.h>
#include <Tactility/service/loader/Loader.h>

#include <lvgl.h>
#include <tactility/hal/Device.h>
#include <tactility/lvgl_fonts.h>

namespace tt::app::poweroff {

extern const AppManifest manifest;

class PowerOffApp final : public App {

    /** Replaces the screen with a plain "powered off" message, forces it to draw
     * synchronously, and waits for the display to confirm the draw physically
     * finished. On e-paper this is what's left showing once power cuts, so it
     * doubles as visual confirmation that the device shut down cleanly rather
     * than crashed or hung. Called from an LVGL button click callback, so it's
     * already running on the LVGL thread. */
    static void showPoweredOffScreenAndWait(hal::display::DisplayDevice* display) {
        auto* screen = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
        lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        auto* title = lv_label_create(screen);
        lv_label_set_text(title, "Tactility OS");
        lv_obj_set_style_text_font(title, lvgl_get_text_font(FONT_SIZE_LARGE), 0);

        auto* subtitle = lv_label_create(screen);
        lv_label_set_text(subtitle, "Powered off");

        lv_screen_load(screen);

        if (display != nullptr) {
            auto* lvgl_display = display->getLvglDisplay();
            if (lvgl_display != nullptr) {
                lv_refr_now(lvgl_display);
            }
            display->waitForFlushComplete();
        }
    }

    static void onYesPressed(lv_event_t* /*event*/) {
        auto power = hal::findFirstDevice<hal::power::PowerDevice>(hal::Device::Type::Power);
        if (power == nullptr || !power->supportsPowerOff()) {
            return;
        }

        auto display = hal::findFirstDevice<hal::display::DisplayDevice>(hal::Device::Type::Display);
        showPoweredOffScreenAndWait(display.get());

        power->powerOff();
    }

    static void onNoPressed(lv_event_t* /*event*/) {
        stop(manifest.appId);
    }

public:

    void onShow(AppContext& /*app*/, lv_obj_t* parent) override {
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        auto* label = lv_label_create(parent);
        lv_label_set_text(label, "Power off?");
        lv_obj_set_style_text_font(label, lvgl_get_text_font(FONT_SIZE_LARGE), 0);

        auto* button_wrapper = lv_obj_create(parent);
        lv_obj_set_flex_flow(button_wrapper, LV_FLEX_FLOW_ROW);
        lv_obj_set_size(button_wrapper, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(button_wrapper, 0, 0);
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
    .appCategory = Category::System,
    .appFlags = AppManifest::Flags::HideStatusBar | AppManifest::Flags::Hidden,
    .createApp = create<PowerOffApp>
};

} // namespace
