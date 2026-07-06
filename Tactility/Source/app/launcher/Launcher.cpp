#include <Tactility/Tactility.h>

#include <Tactility/app/AppContext.h>
#include <Tactility/app/AppPaths.h>
#include <Tactility/app/AppRegistration.h>
#include <Tactility/app/setup/Setup.h>
#include <Tactility/hal/power/PowerDevice.h>
#include <Tactility/service/loader/Loader.h>
#include <Tactility/settings/BootSettings.h>

#include <cstring>
#include <lvgl.h>

#include <tactility/log.h>
#include <tactility/lvgl_fonts.h>
#include <tactility/lvgl_icon_launcher.h>
#include <tactility/lvgl_module.h>

namespace tt::app::launcher {

constexpr auto* TAG = "Launcher";

static uint32_t getButtonPadding(UiDensity density, uint32_t buttonSize) {
    if (density == LVGL_UI_DENSITY_COMPACT) {
        return 0;
    } else {
        return buttonSize / 8;
    }
}

static int32_t computeButtonMargin(int32_t available_span, int32_t total_button_size) {
    const int32_t usable = std::max<int32_t>(0, available_span - (3 * total_button_size));
    return std::min<int32_t>(usable / 16, total_button_size / 2);
}

class LauncherApp final : public App {

    static lv_obj_t* createAppButton(lv_obj_t* parent, UiDensity uiDensity, const char* imageFile, const char* appId, int32_t itemMargin, bool isLandscape) {
        const auto button_size = lvgl_get_launcher_icon_font_height();
        const auto button_padding = getButtonPadding(uiDensity, button_size);
        auto* apps_button = lv_button_create(parent);

        lv_obj_set_style_pad_all(apps_button, static_cast<int32_t>(button_padding), LV_STATE_DEFAULT);
        if (isLandscape) {
            lv_obj_set_style_margin_hor(apps_button, itemMargin, LV_STATE_DEFAULT);
        } else {
            lv_obj_set_style_margin_ver(apps_button, itemMargin, LV_STATE_DEFAULT);
        }

        lv_obj_set_style_shadow_width(apps_button, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(apps_button, 0, LV_STATE_DEFAULT);

        // create the image first
        auto* button_image = lv_image_create(apps_button);
        lv_obj_set_style_text_font(button_image, lvgl_get_launcher_icon_font(), LV_STATE_DEFAULT);
        lv_image_set_src(button_image, imageFile);
        lv_obj_set_style_text_color(button_image, lv_theme_get_color_primary(button_image), LV_STATE_DEFAULT);
        lv_obj_set_style_image_recolor(button_image, lv_theme_get_color_primary(parent), LV_STATE_DEFAULT);
        lv_obj_set_style_image_recolor_opa(button_image, LV_OPA_COVER, LV_STATE_DEFAULT);

        // Ensure it's square (Material Symbols are slightly wider than tall)
        lv_obj_set_size(button_image, button_size, button_size);

        lv_obj_add_event_cb(apps_button, onAppPressed, LV_EVENT_SHORT_CLICKED, (void*)appId);

        return apps_button;
    }

    static bool shouldShowPowerButton() {
        bool show_power_button = false;
        hal::findDevices<hal::power::PowerDevice>(hal::Device::Type::Power, [&show_power_button](const auto& device) {
            if (device->supportsPowerOff()) {
                show_power_button = true;
                return false; // stop iterating
            } else {
                return true; // continue iterating
            }
        });
        return show_power_button;
    }

    static void onAppPressed(lv_event_t* e) {
        auto* appId = static_cast<const char*>(lv_event_get_user_data(e));
        start(appId);
    }

    static void onPowerOffPressed(lv_event_t* /*e*/) {
        start("PowerOff");
    }

    // The screen object outlives the launcher's views (it's recreated by GuiService::redraw()
    // via lv_obj_clean() on every app switch), so the LV_EVENT_SIZE_CHANGED callback registered
    // on it must be removed once buttons_wrapper is destroyed, to avoid a dangling user-data
    // pointer on the next rotation while a different app is visible.
    static void onButtonsWrapperDeleted(lv_event_t* e) {
        auto* buttons_wrapper = lv_event_get_target_obj(e);
        auto* screen = lv_obj_get_screen(buttons_wrapper);
        lv_obj_remove_event_cb_with_user_data(screen, onButtonsWrapperResized, buttons_wrapper);
    }

    // Re-applies the flex direction and per-button margins when the display orientation
    // changes while the launcher is the visible app (these are decided once at onShow()
    // based on the resolution at that time, so a later rotation needs this to catch up).
    static void onButtonsWrapperResized(lv_event_t* e) {
        auto* buttons_wrapper = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
        const auto* display = lv_obj_get_display(buttons_wrapper);

        const auto button_size = lvgl_get_launcher_icon_font_height();
        const auto button_padding = getButtonPadding(lvgl_get_ui_density(), button_size);
        const auto total_button_size = button_size + (button_padding * 2);

        const auto horizontal_px = lv_display_get_horizontal_resolution(display);
        const auto vertical_px = lv_display_get_vertical_resolution(display);
        const bool is_landscape_display = horizontal_px >= vertical_px;
        const auto current_flow = lv_obj_get_style_flex_flow(buttons_wrapper, LV_PART_MAIN);
        const bool was_landscape = current_flow == LV_FLEX_FLOW_ROW;
        if (is_landscape_display == was_landscape) {
            return;
        }

        lv_obj_set_flex_flow(buttons_wrapper, is_landscape_display ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);

        const int32_t margin = is_landscape_display
            ? computeButtonMargin(horizontal_px, total_button_size)
            : computeButtonMargin(vertical_px, total_button_size);

        const uint32_t child_count = lv_obj_get_child_count(buttons_wrapper);
        for (uint32_t i = 0; i < child_count; i++) {
            auto* button = lv_obj_get_child(buttons_wrapper, i);
            lv_obj_set_style_margin_hor(button, is_landscape_display ? margin : 0, LV_STATE_DEFAULT);
            lv_obj_set_style_margin_ver(button, is_landscape_display ? 0 : margin, LV_STATE_DEFAULT);
        }
    }

public:

    void onCreate(AppContext& app) override {
        settings::BootSettings boot_properties;
        if (
            // Auto-start due to built-in requirement
            strcmp(CONFIG_TT_AUTO_START_APP_ID, "") != 0 &&
            findAppManifestById(CONFIG_TT_AUTO_START_APP_ID) != nullptr
        ) {
            LOG_I(TAG, "Starting %s", CONFIG_TT_AUTO_START_APP_ID);
            start(CONFIG_TT_AUTO_START_APP_ID);
        } else if (
            // Auto-start due to user configuration
            settings::loadBootSettings(boot_properties) &&
            !boot_properties.autoStartAppId.empty() &&
            findAppManifestById(boot_properties.autoStartAppId) != nullptr
        ) {
            LOG_I(TAG, "Starting %s", boot_properties.autoStartAppId.c_str());
            start(boot_properties.autoStartAppId);
        } else {
            // No auto-start, consider running system setup
            if (!setup::isCompleted()) {
                setup::start();
            }
        }
    }

    void onShow(AppContext& app, lv_obj_t* parent) override {
        auto* buttons_wrapper = lv_obj_create(parent);

        auto ui_density = lvgl_get_ui_density();
        const auto button_size = lvgl_get_launcher_icon_font_height();
        const auto button_padding = getButtonPadding(ui_density, button_size);
        const auto total_button_size = button_size + (button_padding * 2);

        lv_obj_align(buttons_wrapper, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_size(buttons_wrapper, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_border_width(buttons_wrapper, 0, LV_STATE_DEFAULT);
        lv_obj_set_flex_grow(buttons_wrapper, 1);

        // Fix for button selection
        lv_obj_set_style_pad_all(buttons_wrapper, 6, LV_STATE_DEFAULT);

        const auto* display = lv_obj_get_display(parent);
        const auto horizontal_px = lv_display_get_horizontal_resolution(display);
        const auto vertical_px = lv_display_get_vertical_resolution(display);
        const bool is_landscape_display = horizontal_px >= vertical_px;
        if (is_landscape_display) {
            lv_obj_set_flex_flow(buttons_wrapper, LV_FLEX_FLOW_ROW);
        } else {
            lv_obj_set_flex_flow(buttons_wrapper, LV_FLEX_FLOW_COLUMN);
        }

        const int32_t margin = is_landscape_display
            ? computeButtonMargin(lv_display_get_horizontal_resolution(display), total_button_size)
            : computeButtonMargin(lv_display_get_vertical_resolution(display), total_button_size);

        createAppButton(buttons_wrapper, ui_density, LVGL_ICON_LAUNCHER_APPS, "AppList", margin, is_landscape_display);
        createAppButton(buttons_wrapper, ui_density, LVGL_ICON_LAUNCHER_FOLDER, "Files", margin, is_landscape_display);
        createAppButton(buttons_wrapper, ui_density, LVGL_ICON_LAUNCHER_SETTINGS, "Settings", margin, is_landscape_display);

        // The launcher's container is several levels below the screen, and LVGL only sends
        // LV_EVENT_SIZE_CHANGED to the screen object itself on a resolution change - so the
        // handler is attached there, with buttons_wrapper passed through as user data.
        lv_obj_add_event_cb(lv_obj_get_screen(parent), onButtonsWrapperResized, LV_EVENT_SIZE_CHANGED, buttons_wrapper);
        lv_obj_add_event_cb(buttons_wrapper, onButtonsWrapperDeleted, LV_EVENT_DELETE, nullptr);

        if (shouldShowPowerButton()) {
            auto* power_button = lv_button_create(parent);
            lv_obj_set_style_pad_all(power_button, 8, 0);
            lv_obj_align(power_button, LV_ALIGN_BOTTOM_MID, 0, -10);
            lv_obj_add_event_cb(power_button, onPowerOffPressed, LV_EVENT_SHORT_CLICKED, nullptr);
            lv_obj_set_style_shadow_width(power_button, 0, LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(power_button, 0, LV_PART_MAIN);

            auto* power_label = lv_label_create(power_button);
            lv_label_set_text(power_label, LV_SYMBOL_POWER);
            lv_obj_set_style_text_color(power_label, lv_theme_get_color_primary(parent), LV_STATE_DEFAULT);
        }
    }
};

extern const AppManifest manifest = {
    .appId = "Launcher",
    .appName = "Launcher",
    .appCategory = Category::System,
    .appFlags = AppManifest::Flags::Hidden,
    .createApp = create<LauncherApp>
};

LaunchId start() {
    return app::start(manifest.appId);
}

} // namespace
