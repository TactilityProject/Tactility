// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The look of the tiles */
typedef enum {
    /** The icon above the name */
    LVGL_TILE_GRID_STYLE_TILE,
    /** A card with the icon on the left and the name next to it */
    LVGL_TILE_GRID_STYLE_CARD,
} LvglTileGridStyle;

/** The colour of single colour icons (glyphs, and images with recolor) */
typedef enum {
    /** The theme's icon colour */
    LVGL_TILE_GRID_ICON_COLOR_DEFAULT,
    /** The theme's primary colour */
    LVGL_TILE_GRID_ICON_COLOR_PRIMARY,
    /** The theme's secondary colour */
    LVGL_TILE_GRID_ICON_COLOR_SECONDARY,
} LvglTileGridIconColor;

/** Where the page dots are */
typedef enum {
    /** A row below the tiles */
    LVGL_TILE_GRID_INDICATOR_BELOW,
    /** A column at the left of the tiles. The same room is left free at the right, so the tiles stay centered. */
    LVGL_TILE_GRID_INDICATOR_LEFT,
    /** A column at the right of the tiles. The same room is left free at the left, so the tiles stay centered. */
    LVGL_TILE_GRID_INDICATOR_RIGHT,
} LvglTileGridIndicator;

/** An item of the grid, as it is passed to the callbacks */
typedef struct {
    /** Identifies the item across rebuilds, e.g. to keep it selected */
    const char* id;
    const char* name;
    /** A glyph of the shared icon font, or NULL to resolve it with resolve_icon when the tile is shown */
    const char* icon;
    /** The name is shown in the theme's secondary colour */
    bool highlighted;
    /** Shown instead of the icon glyph when set. Must outlive the tile. */
    const lv_image_dsc_t* image;
    /** Paints the image in the icon colour, for single colour images. Also applies to an image from resolve_image. */
    bool image_recolor;
    /** For the owner of the grid, e.g. an index into its own data */
    void* user_data;
} LvglTileGridItem;

/** What the grid asks of and reports to its owner. Only collect and on_clicked are required. */
typedef struct {
    /** Adds the items in display order with lvgl_tile_grid_add_item(). Called on every rebuild. */
    void (*collect)(lv_obj_t* grid, void* user_data);
    /** A tile was clicked (not swiped) */
    void (*on_clicked)(lv_obj_t* grid, const LvglTileGridItem* item, void* user_data);
    void (*on_long_pressed)(lv_obj_t* grid, const LvglTileGridItem* item, void* user_data);
    /** A key was pressed on a tile */
    void (*on_key)(lv_obj_t* grid, const LvglTileGridItem* item, uint32_t key, void* user_data);
    /** Returns the image of an item without an icon or an image. Only called for the items on the shown page. */
    const lv_image_dsc_t* (*resolve_image)(lv_obj_t* grid, const LvglTileGridItem* item, void* user_data);
    /** Returns the icon glyph of an item without an icon or an image, when resolve_image gave none. Only called for the items on the shown page. */
    const char* (*resolve_icon)(lv_obj_t* grid, const LvglTileGridItem* item, void* user_data);
    /** The user went to another page */
    void (*on_page_changed)(lv_obj_t* grid, void* user_data);
    /** Called on every rebuild, after the old tiles were deleted and before collect. Nothing shows the images of earlier items anymore. */
    void (*on_tiles_cleared)(lv_obj_t* grid, void* user_data);
    void* user_data;
} LvglTileGridCallbacks;

/**
 * Adapts the paging to its surroundings, like system bars with page buttons. All functions are optional.
 * They get the grid only: they belong to the environment, not to the owner of the grid.
 */
typedef struct {
    /** Where the page dots go. Asked on every rebuild. Without it, they are below the tiles. */
    LvglTileGridIndicator (*get_indicator)(lv_obj_t* grid);
    /** Whether a swipe up and down changes the page instead of left and right. Asked on every swipe. */
    bool (*is_vertical_swipe)(lv_obj_t* grid);
    /** Whether a swipe must be ignored, e.g. when it started in the zone that opens a system menu */
    bool (*ignore_swipe)(lv_obj_t* grid);
    /** The grid got multiple pages (true) or one page (false), e.g. to show page buttons that call lvgl_tile_grid_next_page() */
    void (*on_paging_visible)(lv_obj_t* grid, bool visible);
} LvglTileGridPaging;

/** The configuration of a grid. Zero means the defaults: tiles in the icon colour, no swipe navigation and no paging adapter. */
typedef struct {
    LvglTileGridStyle style;
    LvglTileGridIconColor icon_color;
    /** Swiping over the grid goes to the next or previous page */
    bool swipe_navigation;
    /** Copied */
    LvglTileGridCallbacks callbacks;
    /** May be NULL. Must outlive the grid. */
    const LvglTileGridPaging* paging;
    /** The page that is shown first, e.g. the one of lvgl_tile_grid_get_page() before the grid was rebuilt. Limited to the last page. */
    uint32_t page;
} LvglTileGridConfig;

/**
 * @brief Creates a paged, non-scrolling grid of tiles (an icon above a name, or a card with the icon beside it), with page dots
 * that are hidden when there is only one page. The arrow keys move between the tiles.
 * The grid is as wide as its parent and grows in a flex column (flex grow 1). Its size decides how many tiles fit on a page.
 * It is populated right away, after a layout pass of the parent.
 * @param[in] parent the parent object, normally with a flex column layout
 * @param[in] config the configuration, which is copied
 * @return the created grid
 */
lv_obj_t* lvgl_tile_grid_create(lv_obj_t* parent, const LvglTileGridConfig* config);

/**
 * @brief Adds an item. Only valid during the collect callback.
 * @param[in] grid the grid
 * @param[in] item the item. Its id and name are copied, its icon and image are not.
 */
void lvgl_tile_grid_add_item(lv_obj_t* grid, const LvglTileGridItem* item);

/** @brief Sets the look of the tiles. Applies from the next rebuild. */
void lvgl_tile_grid_set_style(lv_obj_t* grid, LvglTileGridStyle style);

/** @brief Sets the colour of the tile icons. Applies from the next rebuild. */
void lvgl_tile_grid_set_icon_color(lv_obj_t* grid, LvglTileGridIconColor color);

/** @brief Shows the first page from the next rebuild. */
void lvgl_tile_grid_show_first_page(lv_obj_t* grid);

/**
 * @brief Rebuilds the grid asynchronously, so it is safe to call from a tile's own event.
 * @param[in] grid the grid
 * @param[in] keep_selection when true, the selected item stays selected and its page is shown
 */
void lvgl_tile_grid_request_repopulate(lv_obj_t* grid, bool keep_selection);

/**
 * @brief Like lvgl_tile_grid_request_repopulate(grid, false), and it selects the first tile when the grid has any, and otherwise the fallback.
 * @param[in] grid the grid
 * @param[in] empty_fallback the widget to select when the grid is empty, directly or through its grid navigation container. May be NULL.
 */
void lvgl_tile_grid_request_repopulate_selecting_first(lv_obj_t* grid, lv_obj_t* empty_fallback);

/**
 * @brief Replaces the fallback widget of a pending selection, e.g. when the widget was deleted. Does nothing without a pending selection.
 */
void lvgl_tile_grid_replace_empty_fallback(lv_obj_t* grid, lv_obj_t* empty_fallback);

/** @brief Goes to the next page, or the first page after the last. Rebuilds right away, so call it outside of a tile's own event. */
void lvgl_tile_grid_next_page(lv_obj_t* grid);

/** @brief Goes to the previous page, or the last page before the first. Rebuilds right away, so call it outside of a tile's own event. */
void lvgl_tile_grid_previous_page(lv_obj_t* grid);

/** @return the zero-based index of the shown page */
uint32_t lvgl_tile_grid_get_page(lv_obj_t* grid);

/** @return the tile of the item with this id on the shown page, or NULL */
lv_obj_t* lvgl_tile_grid_find_tile(lv_obj_t* grid, const char* id);

/** @return the item of a tile, or NULL when it is not a tile of the grid */
const LvglTileGridItem* lvgl_tile_grid_get_tile_item(lv_obj_t* grid, lv_obj_t* tile);

/** @return the grid navigation container that holds the tiles, which is the focus target in the input group */
lv_obj_t* lvgl_tile_grid_get_tiles(lv_obj_t* grid);

/** @return the size in pixels of the square icon of a tile */
uint32_t lvgl_tile_grid_get_tile_icon_size(void);

/** @return the size in pixels of the square icon of a card */
uint32_t lvgl_tile_grid_get_card_icon_size(void);

#ifdef __cplusplus
}
#endif
