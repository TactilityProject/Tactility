#pragma once

#include <lvgl.h>

#include <cstdint>
#include <string>
#include <vector>

namespace tt::app {

struct TileGridItem {
    /** Identifies the item across rebuilds, e.g. to keep it selected */
    std::string id;
    std::string name;
    /** Shared icon font glyph, or nullptr to resolve it with Callbacks::resolveIcon when the tile is shown */
    const char* icon;
    /** Marked with a badge */
    bool highlighted;
};

/**
 * Paged, non-scrolling grid of tiles (icon above name) with "<" and ">" page buttons.
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
        /** Optional: returns the icon of an item without one. Only called for the items on the shown page. */
        const char* (*resolveIcon)(const TileGridItem& item, void* userData);
        /** Optional: called after the user went to another page */
        void (*onPageChanged)(void* userData);
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

    /** Sets the colour of the tile icons. Applies from the next (re)population. */
    void setIconColor(IconColor color) { iconColor = color; }

    /** Shows the first page from the next (re)population. */
    void showFirstPage() { page = 0; }

    /** Swiping left or right over the grid goes to the next or previous page. Call before creating the widgets. */
    void setSwipeNavigation(bool enabled) { swipeNavigation = enabled; }

    /**
     * Creates the grid and populates it. Call setPageButtons() right after it to add page buttons.
     * @param[in] parent the container for the grid, with a flex column layout
     */
    void createWidgets(lv_obj_t* parent);

    /**
     * Creates the grid and populates it, with a bar below it: buttons on the left (see addBarButton()),
     * a page indicator in the center and page buttons on the right. The page indicator and buttons are
     * hidden when there's only one page, and the page buttons wrap around.
     * @param[in] parent the container for the grid and the bar
     */
    void createWidgetsWithBottomBar(lv_obj_t* parent);

    /**
     * Makes the buttons go to the previous and next page. They are hidden when there's only one page, and wrap around.
     * Call right after createWidgets().
     */
    void setPageButtons(lv_obj_t* previous, lv_obj_t* next);

    /**
     * Adds an icon button to the left side of the bottom bar. Call right after createWidgetsWithBottomBar().
     * @param[in] icon a shared icon (see lvgl/icons/shared.h)
     * @return the button
     */
    lv_obj_t* addBarButton(const char* icon, lv_event_cb_t onClicked, void* userData);

    /**
     * Rebuilds the grid asynchronously, so it is safe to call from a tile's own event.
     * @param[in] keepSelection when true, the selected item stays selected and its page is shown
     */
    void requestRepopulate(bool keepSelection);

    /** @return the tile of the item on the shown page, or nullptr */
    lv_obj_t* findTile(const std::string& id) const;

private:

    Callbacks callbacks;
    std::vector<TileGridItem> items;
    uint32_t page = 0;
    uint32_t pageCount = 1;
    IconColor iconColor = IconColor::Default;
    bool swipeNavigation = false;
    bool keepSelectionOnRepopulate = true;
    lv_obj_t* grid = nullptr;
    /** The grid size that the current tiles were laid out for */
    int32_t populatedWidth = 0;
    int32_t populatedHeight = 0;
    lv_obj_t* prevButton = nullptr;
    lv_obj_t* nextButton = nullptr;
    lv_obj_t* pageIndicator = nullptr;
    lv_obj_t* bottomBar = nullptr;
    lv_obj_t* barSpacer = nullptr;

    void createGrid(lv_obj_t* parent);
    void finishCreate(lv_obj_t* parent);
    void goToPreviousPage();
    void goToNextPage();
    void populate();

    static void onDeferredRepopulate(void* userData);
    static void onDeferredResize(void* userData);
    static void onDeferredNextPage(void* userData);
    static void onDeferredPreviousPage(void* userData);
    static void onGridDeleted(lv_event_t* e);
    static void onGridSizeChanged(lv_event_t* e);
    static void onPrevPressed(lv_event_t* e);
    static void onNextPressed(lv_event_t* e);
    static void onGridGesture(lv_event_t* e);
    static void onBottomBarFocused(lv_event_t* e);
    static void onTileClicked(lv_event_t* e);
    static void onTileLongPressed(lv_event_t* e);
    static void onTileKey(lv_event_t* e);
};

}
