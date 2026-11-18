#pragma once

#include <cstdint>

namespace tt::lvgl {

/**
 * Shows the system menu with the settings toggles and the notifications in the window manager's overlay layer.
 * @param[in] swipeX where a swipe down started, in display pixels, or -1 when it wasn't a swipe.
 * A swipe from the left half shows the settings and one from the right half shows the notifications.
 * @warning Caller must hold the LVGL lock.
 */
void systemMenuShow(int32_t swipeX);

/**
 * Shows the system menu on the notifications.
 * @warning Caller must hold the LVGL lock.
 */
void systemMenuShowNotifications();

/**
 * @param[in] uptimeMs a time in milliseconds since boot
 * @return true when the notifications of the menu are open, or were opened or closed at or after that time,
 * which means that the user had the chance to see what arrived at that time
 * @warning Caller must hold the LVGL lock.
 */
bool systemMenuNotificationsSeenSince(uint64_t uptimeMs);

}
