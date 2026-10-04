#pragma once

#include <tactility/freertos/freertos.h>

namespace tt::app::boot {

/**
 * Shows the splash, prepares the system and starts LVGL and the launcher.
 * Halts on an error screen when the boot can't continue.
 * @param[in] startTime the ticks at which the boot app started, for the minimal splash duration
 * @return true when the boot completed and the next app was started
 */
bool bootInit(TickType_t startTime);

}
