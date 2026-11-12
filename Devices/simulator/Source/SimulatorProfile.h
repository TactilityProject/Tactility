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
    uint16_t dpi; // lvgl.dpi from device.properties (if set), otherwise display.dpi
    /** The default rotation: the display is taller than wide. The width and height values may be given in either order. */
    bool portrait;
    /** Compact UI density */
    bool compact;
    /** Greyscale display, which makes the monochrome theme apply */
    bool mono;
    bool keyboard;
    bool touch;
};

constexpr size_t PROFILE_COUNT = 6;

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
 */
bool saveSelection(size_t profileIndex, OrientationOverride orientation);

void setProcessArguments(char** argv);

/** Restarts the process. Returns only when that failed. */
void reboot();

}
