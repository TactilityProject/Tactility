#pragma once

#include <Tactility/app/TileGrid.h>

#include <cstdint>
#include <vector>

namespace tt::app::launcher {

/** The content of the launcher grid and its tile interactions */
struct LauncherMode {
    /** Represents this mode on the button that switches to it */
    const char* buttonIcon;
    /** The tile icon colour for themes that aren't monochrome */
    TileGrid::IconColor iconColor;
    /** Returns the items in display order */
    std::vector<TileGridItem> (*collect)();
    /** Optional */
    void (*onLongPressed)(TileGrid& grid, const TileGridItem& item);
    /** Optional */
    void (*onKey)(TileGrid& grid, const TileGridItem& item, uint32_t key);
};

extern const LauncherMode APPS_MODE;
extern const LauncherMode SETTINGS_MODE;

}
