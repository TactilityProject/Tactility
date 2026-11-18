#pragma once

#include <app/manifest.h>

#include <cstddef>
#include <cstdint>

namespace simulator {

extern const ::AppManifest profileAppManifest;

struct Profile {
    const char* name;
    uint16_t width;
    uint16_t height;
    /** display.dpi from device.properties */
    uint16_t dpi;
    /** The default rotation: the display is taller than wide. The width and height values may be given in either order. */
    bool portrait;
    /** The default font size in pixels of the device, from lvgl.fontSize, or derived from display.dpi when the device doesn't set it */
    uint16_t fontSize;
    /** LVGL UI spacing, from lvgl.uiSpacing of the device */
    uint8_t uiSpacing;
    /** LVGL UI size, from lvgl.uiSize of the device */
    uint8_t uiSize;
    /** Scale of the launcher's icon size, from launcher.iconSizeScale of the device, 1 for the default */
    float launcherIconSizeScale;
    /** Scale of the spacing between the launcher's icons, from launcher.iconSpacingScale of the device, 1 for the default */
    float launcherIconSpacingScale;
    /** Greyscale display, which makes the monochrome theme apply */
    bool mono;
    bool keyboard;
    bool touch;
};

constexpr size_t PROFILE_COUNT = 7;

/** Overrides the default orientation of the selected profile */
enum class OrientationOverride {
    /** The orientation of the profile */
    Default,
    Landscape,
    Portrait
};

const Profile& getProfile(size_t index);

/** Reads the selected profile and makes it the active one. Call once at startup. */
void initActiveProfile();

/** The profile that this run uses */
size_t getActiveProfileIndex();

/** The profile that the next run uses */
size_t loadSelectedProfileIndex();

/** The orientation override that this run uses */
OrientationOverride getActiveOrientation();

/** The orientation override that the next run uses */
OrientationOverride loadSelectedOrientation();

/**
 * Saves the profile and orientation for the next run. A change also sets the saved orientation of the Display settings:
 * the override, or none for the default so that the profile's own orientation applies.
 * A different profile also removes the theming overrides of the Appearance settings.
 */
bool saveSelection(size_t profileIndex, OrientationOverride orientation);

void setProcessArguments(char** argv);

/** Restarts the process. Returns only when that failed. */
void reboot();

}
