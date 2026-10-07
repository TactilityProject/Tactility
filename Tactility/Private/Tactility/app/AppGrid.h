#pragma once

#include <app/manifest.h>

#include <lvgl.h>

#include <cstdint>
#include <vector>

namespace tt::app {

struct AppGridItem {
    /** Owned copy, so a tile never references the app ledger directly */
    ::AppManifest manifest;
    /** Shared icon font glyph */
    const char* icon;
    /** Rendered with an accent colour when not selected */
    bool highlighted;
};

/**
 * Paged, non-scrolling grid of app tiles (icon above name) with "<" and ">" page buttons.
 * Must outlive the widgets it creates.
 */
class AppGrid final {

public:

    struct Callbacks {
        /** Returns the items in display order. Called on every rebuild. */
        std::vector<AppGridItem> (*collect)(void* userData);
        void (*onClicked)(const ::AppManifest& manifest, void* userData);
        /** Optional */
        void (*onLongPressed)(const ::AppManifest& manifest, void* userData);
        /** Optional */
        void (*onKey)(const ::AppManifest& manifest, uint32_t key, void* userData);
        void* userData;
    };

    explicit AppGrid(const Callbacks& callbacks) : callbacks(callbacks) {}

    enum class IconColor {
        /** The theme's icon colour */
        Default,
        /** The theme's primary colour */
        Primary,
        /** The theme's secondary colour */
        Secondary
    };

    /** Sets the colour of the tile icons. Call before creating the widgets. */
    void setIconColor(IconColor color) { iconColor = color; }

    /** Swiping left or right over the grid goes to the next or previous page. Call before creating the widgets. */
    void setSwipeNavigation(bool enabled) { swipeNavigation = enabled; }


    /**
     * Creates the grid and populates it, with a bar below it: buttons on the left (see addBarButton()),
     * a page indicator in the center and page buttons on the right. The page indicator and buttons are
     * hidden when there's only one page, and the page buttons wrap around.
     * @param[in] parent the container for the grid and the bar
     */
    void createWidgetsWithBottomBar(lv_obj_t* parent);

    /**
     * Adds an icon button to the left side of the bottom bar. Call right after createWidgetsWithBottomBar().
     * @param[in] icon a shared icon (see lvgl/icons/shared.h)
     * @return the button
     */
    lv_obj_t* addBarButton(const char* icon, lv_event_cb_t onClicked, void* userData);

    /**
     * Rebuilds the grid asynchronously, so it is safe to call from a tile's own event.
     * @param[in] keepSelection when true, the selected app stays selected and its page is shown
     */
    void requestRepopulate(bool keepSelection);

private:

    Callbacks callbacks;
    std::vector<AppGridItem> items;
    uint32_t page = 0;
    uint32_t pageCount = 1;
    IconColor iconColor = IconColor::Default;
    bool swipeNavigation = false;
    bool keepSelectionOnRepopulate = true;
    lv_obj_t* grid = nullptr;
    lv_obj_t* prevButton = nullptr;
    lv_obj_t* nextButton = nullptr;
    lv_obj_t* pageIndicator = nullptr;
    lv_obj_t* pageButtons = nullptr;
    lv_obj_t* barButtons = nullptr;

    void createGrid(lv_obj_t* parent);
    void goToPreviousPage();
    void goToNextPage();
    void populate();

    static void onDeferredRepopulate(void* userData);
    static void onDeferredNextPage(void* userData);
    static void onDeferredPreviousPage(void* userData);
    static void onGridDeleted(lv_event_t* e);
    static void onGridSizeChanged(lv_event_t* e);
    static void onPrevPressed(lv_event_t* e);
    static void onNextPressed(lv_event_t* e);
    static void onGridGesture(lv_event_t* e);
    static void onTileClicked(lv_event_t* e);
    static void onTileLongPressed(lv_event_t* e);
    static void onTileKey(lv_event_t* e);
};

}
