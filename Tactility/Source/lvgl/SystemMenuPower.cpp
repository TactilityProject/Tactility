#include <Tactility/lvgl/SystemMenuPower.h>

#include <Tactility/app/shutdown/Shutdown.h>

#include <lvgl/fonts.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/icons/shared.h>
#include <lvgl/theme.h>
#include <lvgl_window_manager/window_manager.h>

#include <localisation/tr.h>

namespace tt::lvgl {

bool systemMenuHasPowerOptions() {
    return app::shutdown::supportsStandby() || app::shutdown::supportsSuspend() || app::shutdown::supportsPowerOff();
}

bool systemMenuPowerToggle(lv_obj_t* powerButton, lv_obj_t* quickPanel, lv_obj_t* powerPanel) {
    const bool showPower = lv_obj_has_state(powerButton, LV_STATE_CHECKED);
    lv_obj_set_hidden(quickPanel, showPower);
    lv_obj_set_hidden(powerPanel, !showPower);
    return showPower;
}

static void onPowerOptionClicked(lv_event_t* event) {
    const auto* mode = static_cast<const char*>(lv_event_get_user_data(event));
    // The app of an option stops the UI, which the overlay would cover
    window_manager_overlay_hide();
    app::shutdown::start(mode);
}

/** A button with an icon in front of its text */
static void createPowerOption(lv_obj_t* parent, const char* icon, const char* text, const char* mode) {
    auto* button = lv_button_create(parent);
    lv_obj_set_width(button, LV_PCT(80));
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(button, LV_DPX(8), LV_STATE_DEFAULT);
    lv_obj_add_event_cb(button, onPowerOptionClicked, LV_EVENT_SHORT_CLICKED, const_cast<char*>(mode));

    auto* icon_label = lv_label_create(button);
    lv_obj_set_style_text_font(icon_label, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
    lv_label_set_text(icon_label, icon);

    auto* text_label = lv_label_create(button);
    lv_label_set_text(text_label, text);
}

/** The options listed vertically and centered in the card */
lv_obj_t* systemMenuPowerPanelCreate(lv_obj_t* parent) {
    auto* panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, LV_PCT(100), 0);
    lv_obj_set_flex_grow(panel, 1);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_row(panel, lvgl_theme_lerp(8, 16, lvgl_theme_get_spacing()), LV_STATE_DEFAULT);

    if (app::shutdown::supportsStandby()) {
        createPowerOption(panel, LVGL_ICON_SHARED_BEDTIME, tr("Standby"), app::shutdown::MODE_STANDBY);
    }
    if (app::shutdown::supportsSuspend()) {
        createPowerOption(panel, LVGL_ICON_SHARED_MODE_STANDBY, tr("Suspend"), app::shutdown::MODE_SUSPEND);
    }
    if (app::shutdown::supportsPowerOff()) {
        createPowerOption(panel, LVGL_ICON_SHARED_POWER_SETTINGS_NEW, tr("Power off"), app::shutdown::MODE_POWER_OFF);
    }
    // The up and down keys move between the options
    lvgl_grid_navigation_add(panel);
    return panel;
}

} // namespace tt::lvgl
