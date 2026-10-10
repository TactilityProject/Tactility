#pragma once

#include <cstdint>

namespace tt::settings::launcher {

/** How the statusbar and the app's navigation bar are placed on screen */
enum class SystemBarsLayout {
    /** The statusbar on top and the navigation bar at the bottom */
    Split,
    /** A single strip on the left side of the screen */
    Side
};

enum class SystemBarsMode {
    /** Side for wide landscape screens, Split otherwise */
    Auto,
    /** The layouts that the user picked for portrait and landscape */
    Custom
};

/** The system bars layouts that the device allows in an orientation */
enum class SystemBarsCapability {
    /** The layout follows the settings */
    Any,
    Split,
    Side
};

struct SystemBarsCapabilities {
    /** Used for portrait (including square) screens */
    SystemBarsCapability portrait;
    SystemBarsCapability landscape;
};

struct LauncherSettings {
    SystemBarsMode systemBarsMode;
    /** Used for portrait (including square) screens in SystemBarsMode::Custom */
    SystemBarsLayout portraitLayout;
    /** Used for landscape screens in SystemBarsMode::Custom */
    SystemBarsLayout landscapeLayout;
};

bool load(LauncherSettings& settings);

LauncherSettings loadOrGetDefault();

LauncherSettings getDefault();

bool save(const LauncherSettings& settings);

/** @return the system bars layouts that this device's configuration allows */
SystemBarsCapabilities getSystemBarsCapabilities();

/**
 * @return the system bars layout for a screen of the given size in its current orientation.
 * A capability other than Any for that orientation decides the layout, instead of the settings.
 */
SystemBarsLayout resolveSystemBarsLayout(const LauncherSettings& settings, const SystemBarsCapabilities& capabilities, int32_t width, int32_t height);

} // namespace tt::settings::launcher
