#pragma once

#include <lvgl.h>
#include <lvgl/widgets/tile_grid.h>

#include <cstdint>
#include <string>
#include <vector>

namespace tt::app {

/**
 * @return false on devices without PSRAM, where SVG icons take too much memory.
 * The tiles then show the icon font glyphs, and the SVG icons are never loaded.
 */
bool areSvgIconsEnabled();

/** @return the size in pixels of the square icon of a tile */
inline uint32_t getTileIconSize() { return lvgl_tile_grid_get_tile_icon_size(); }

/** @return the size in pixels of the square icon of a card */
inline uint32_t getCardIconSize() { return lvgl_tile_grid_get_card_icon_size(); }

/** An item of a tile grid that owns its texts, see LvglTileGridItem */
struct TileGridItem {
    std::string id;
    std::string name;
    /** Shared icon font glyph, or nullptr */
    const char* icon;
    /** Marked in the theme's secondary colour */
    bool highlighted;
    /** Shown instead of the icon glyph when set. Must outlive the tile. */
    const lv_image_dsc_t* image = nullptr;
    /** Paints the image in the icon colour, for single colour images */
    bool imageRecolor = false;
};

/** Adds the items to a grid, during its collect callback */
void addTileGridItems(lv_obj_t* grid, const std::vector<TileGridItem>& items);

/**
 * The paging of a tile grid in an app window, for LvglTileGridConfig::paging:
 * - The page buttons of the system bars are shown while the grid has multiple pages, and change its page.
 * - In the side layout of the system bars, a swipe up or down changes the page, like the page buttons there,
 *   and the page dots are a column next to the strip (on its inner side) instead of a row below the grid.
 * Only one grid can use it at a time, as there is one set of page buttons.
 */
const LvglTileGridPaging* getSystemBarsPaging();

}
