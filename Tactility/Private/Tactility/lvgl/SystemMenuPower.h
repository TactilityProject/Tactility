#pragma once

#include <lvgl.h>

namespace tt::lvgl {

/** @return true when the device can enter standby, suspend or power off */
bool systemMenuHasPowerOptions();

/**
 * Creates the standby, suspend and power off options, listed vertically and centered.
 * @warning Caller must hold the LVGL lock.
 * @param[in] parent the card of the system menu
 */
lv_obj_t* systemMenuPowerPanelCreate(lv_obj_t* parent);

/**
 * Shows the power options instead of the quick settings while the power button is checked.
 * @warning Caller must hold the LVGL lock.
 * @return true when the power options are shown
 */
bool systemMenuPowerToggle(lv_obj_t* powerButton, lv_obj_t* quickPanel, lv_obj_t* powerPanel);

} // namespace tt::lvgl
