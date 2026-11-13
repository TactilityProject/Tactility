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

/** Reads the favourites from their file into memory. File operation, see <tactility/file_ops.h>. */
void loadFavourites(void* context);

/** Rasterizes the icon.svg of every installed package into memory. File operation, see <tactility/file_ops.h>. */
void loadPackageIcons(void* context);

/** Rasterizes the icons of the settings apps into memory, once. File operation, see <tactility/file_ops.h>. */
void loadSettingsIcons(void* context);

/** @return true after loadSettingsIcons() ran */
bool areSettingsIconsLoaded();

/** @return false when the app icons were rasterized for another theme (mono or colour) than the current one */
bool areAppIconsCurrent();

/** @return false when the settings icons are not loaded, or were rasterized for another theme than the current one */
bool areSettingsIconsCurrent();

/**
 * The icon of apps that have none. Available after loadPackageIcons() ran, nullptr before.
 * @param[out] recolor true when the icon is single colour and should be painted in the theme's icon colour
 */
const lv_image_dsc_t* defaultAppIcon(bool* recolor);

extern const LauncherMode APPS_MODE;
extern const LauncherMode SETTINGS_MODE;

}
