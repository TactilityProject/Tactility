#pragma once

#include <lvgl.h>

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
uint32_t getTileIconSize();

/** @return the size in pixels of the square icon of a card */
uint32_t getCardIconSize();

struct TileGridItem {
    /** Identifies the item across rebuilds, e.g. to keep it selected */
    std::string id;
    std::string name;
    /** Shared icon font glyph, or nullptr to resolve it with Callbacks::resolveIcon when the tile is shown */
    const char* icon;
    /** Marked with a badge */
    bool highlighted;
    /** Shown instead of the icon glyph when set. Must outlive the tile. */
    const lv_image_dsc_t* image = nullptr;
    /** Paints the image in the icon colour, for single colour images. Also applies to an image from Callbacks::resolveImage. */
    bool imageRecolor = false;
};

/**
 * Paged, non-scrolling grid of tiles (icon above name, or a card with the icon beside it) with "<" and ">" page buttons.
 * Must outlive the widgets it creates.
 */
class TileGrid final {

public:

    struct Callbacks {
        /** Returns the items in display order. Called on every rebuild. */
        std::vector<TileGridItem> (*collect)(void* userData);
        void (*onClicked)(const TileGridItem& item, void* userData);
        /** Optional */
        void (*onLongPressed)(const TileGridItem& item, void* userData);
        /** Optional */
        void (*onKey)(const TileGridItem& item, uint32_t key, void* userData);
        /** Optional: returns the image of an item without an icon or image. Only called for the items on the shown page. */
        const lv_image_dsc_t* (*resolveImage)(const TileGridItem& item, void* userData);
        /** Optional: returns the icon font glyph of an item without an icon or an image, when resolveImage() gave none. Only called for the items on the shown page. */
        const char* (*resolveIcon)(const TileGridItem& item, void* userData);
        /** Optional: called after the user went to another page */
        void (*onPageChanged)(void* userData);
        /** Optional: called on every rebuild, after the old tiles were deleted and before collect. Nothing shows the images of earlier items anymore. */
        void (*onTilesCleared)(void* userData);
        void* userData;
    };

    explicit TileGrid(const Callbacks& callbacks) : callbacks(callbacks) {}

    enum class IconColor {
        /** The theme's icon colour */
        Default,
        /** The theme's primary colour */
        Primary,
        /** The theme's secondary colour */
        Secondary
    };

    enum class Style {
        /** The icon above the name */
        Tile,
        /** A card with the icon on the left and the name next to it */
        Card
    };

    /** Sets the look of the tiles. Applies from the next (re)population. */
    void setStyle(Style newStyle) { style = newStyle; }

    /** Sets the colour of the tile icons. Applies from the next (re)population. */
    void setIconColor(IconColor color) { iconColor = color; }

    /** Shows the first page from the next (re)population. */
    void showFirstPage() { page = 0; }

    /** Swiping left or right over the grid goes to the next or previous page. Call before creating the widgets. */
    void setSwipeNavigation(bool enabled) { swipeNavigation = enabled; }

    /** With swipe navigation, a swipe up or down changes the page in the side layout of the system bars. Call before creating the widgets. */
    void setVerticalSwipeInSideLayout(bool enabled) { verticalSwipeInSideLayout = enabled; }

    /**
     * Shows the page buttons of the system bars while the grid has multiple pages, and makes them change the page.
     * Call before creating the widgets.
     */
    void setSystemPageButtons(bool enabled) { systemPageButtons = enabled; }

    /**
     * In the side layout of the system bars, the page indicator is a column of dots next to the strip (on its inner side),
     * instead of a row below the grid. Call before creating the widgets.
     */
    void setSideIndicator(bool enabled) { sideIndicator = enabled; }

    /**
     * Creates the grid and populates it, with a page indicator below it that is hidden when there's only one page.
     * @param[in] parent the container for the grid, with a flex column layout
     */
    void createWidgets(lv_obj_t* parent);

    /** Forgets the widgets. Call when the parent's widgets are deleted. */
    void destroyWidgets();

    /**
     * Rebuilds the grid asynchronously, so it is safe to call from a tile's own event.
     * @param[in] keepSelection when true, the selected item stays selected and its page is shown
     */
    void requestRepopulate(bool keepSelection);

    /**
     * Like requestRepopulate(false), and it selects the first tile when the grid has any, and otherwise the fallback.
     * @param[in] emptyFallback the widget to select when the grid is empty, directly or through its grid navigation container
     */
    void requestRepopulateSelectingFirst(lv_obj_t* emptyFallback);

    /**
     * Replaces the fallback widget of a pending selection, e.g. when the widget was deleted. Does nothing without a pending selection.
     * @param[in] emptyFallback the widget to select when the grid is empty
     */
    void replaceEmptyFallback(lv_obj_t* emptyFallback);

    /** @return the tile of the item on the shown page, or nullptr */
    lv_obj_t* findTile(const std::string& id) const;

private:
    /** Where the pointer was pressed on a tile, which tells a tap from a swipe when it is released */
    lv_point_t pressPoint = {};

    Callbacks callbacks;
    std::vector<TileGridItem> items;
    uint32_t page = 0;
    uint32_t pageCount = 1;
    IconColor iconColor = IconColor::Default;
    Style style = Style::Tile;
    bool swipeNavigation = false;
    bool systemPageButtons = false;
    bool verticalSwipeInSideLayout = false;
    bool sideIndicator = false;
    bool keepSelectionOnRepopulate = true;
    bool selectFirstOnRepopulate = false;
    lv_obj_t* emptyFallback = nullptr;
    lv_obj_t* grid = nullptr;
    /** The grid size that the current tiles were laid out for */
    int32_t populatedWidth = 0;
    int32_t populatedHeight = 0;
    lv_obj_t* pageIndicator = nullptr;

    void createGrid(lv_obj_t* parent);
    void finishCreate(lv_obj_t* parent);
    void goToPreviousPage();
    void goToNextPage();
    void populate();
    bool isPagingVisible() const;
    void setPagingVisible(bool visible);

    static void onDeferredRepopulate(void* userData);
    static void onDeferredResize(void* userData);
    static void onDeferredNextPage(void* userData);
    static void onDeferredPreviousPage(void* userData);
    static void onGridDeleted(lv_event_t* e);
    static void onGridSizeChanged(lv_event_t* e);
    static void onSystemPreviousPage(void* userData);
    static void onSystemNextPage(void* userData);
    static void onGridGesture(lv_event_t* e);
    static void onTileStateChanged(lv_event_t* e);
    static void onTilePressed(lv_event_t* e);
    static void onTileClicked(lv_event_t* e);
    static void onTileLongPressed(lv_event_t* e);
    static void onTileKey(lv_event_t* e);
};

}
