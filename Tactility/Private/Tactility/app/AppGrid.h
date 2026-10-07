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

    /** Shows the tile icons in the theme's primary color. Call before creating the widgets. */
    void setPrimaryColorIcons(bool enabled) { primaryColorIcons = enabled; }

    /** Adds the paging buttons to the toolbar, creates the grid below it and populates it. */
    void createWidgets(lv_obj_t* parent, lv_obj_t* toolbar);

    /**
     * Creates the grid and populates it. The page number and the page buttons go in pageBar, which is hidden when there's only one page. The page buttons wrap around.
     * @param[in] parent the container for the grid
     * @param[in] pageBar an empty container
     */
    void createWidgetsWithPageBar(lv_obj_t* parent, lv_obj_t* pageBar);

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
    bool primaryColorIcons = false;
    bool keepSelectionOnRepopulate = true;
    lv_obj_t* grid = nullptr;
    lv_obj_t* prevButton = nullptr;
    lv_obj_t* nextButton = nullptr;
    lv_obj_t* pageBar = nullptr;
    lv_obj_t* pageLabel = nullptr;

    void createGrid(lv_obj_t* parent);
    void populate();

    static void onDeferredRepopulate(void* userData);
    static void onGridDeleted(lv_event_t* e);
    static void onGridSizeChanged(lv_event_t* e);
    static void onPrevPressed(lv_event_t* e);
    static void onNextPressed(lv_event_t* e);
    static void onTileClicked(lv_event_t* e);
    static void onTileLongPressed(lv_event_t* e);
    static void onTileKey(lv_event_t* e);
};

}
