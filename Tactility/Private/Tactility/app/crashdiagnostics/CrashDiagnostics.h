#pragma once

#ifdef ESP_PLATFORM

namespace tt::app::boot {
class BootScreen;
}

namespace tt::app::crashdiagnostics {

/**
 * Writes the crash log and shows the crash with a QR code for reporting it, after a panic reboot.
 * Returns when the user continues with any input, and blocks forever when there's no input device to continue with.
 * @param[in] screen the boot screen, which may have no display
 */
void showCrashScreen(boot::BootScreen& screen);

}

#endif
