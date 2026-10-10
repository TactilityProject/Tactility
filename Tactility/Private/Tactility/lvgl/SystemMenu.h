#pragma once

namespace tt::lvgl {

/**
 * Shows the system menu with quick toggles (Wi-Fi, GPS, Bluetooth) in the window manager's overlay layer.
 * @warning Caller must hold the LVGL lock.
 */
void systemMenuShow();

}
