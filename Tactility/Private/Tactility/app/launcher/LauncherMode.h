#pragma once

#include <Tactility/app/AppGrid.h>

#include <cstdint>
#include <vector>

namespace tt::app::launcher {

/** The content of the launcher grid and its tile interactions */
struct LauncherMode {
    /** Represents this mode on the button that switches to it */
    const char* buttonIcon;
    /** The tile icon colour for themes that aren't monochrome */
    AppGrid::IconColor iconColor;
    /** Returns the items in display order */
    std::vector<AppGridItem> (*collect)();
    /** Optional */
    void (*onLongPressed)(AppGrid& grid, const ::AppManifest& manifest);
    /** Optional */
    void (*onKey)(AppGrid& grid, const ::AppManifest& manifest, uint32_t key);
};

extern const LauncherMode APPS_MODE;
extern const LauncherMode SETTINGS_MODE;

}
