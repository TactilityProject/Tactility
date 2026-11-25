#include <Tactility/app/TileGrid.h>

#include <lvgl_window_manager/system_bars.h>

#include <tactility/memory.h>

namespace tt::app {

bool areSvgIconsEnabled() {
#ifdef ESP_PLATFORM
    return memory_external_total() > 0;
#else
    return true;
#endif
}

namespace {

LvglTileGridIndicator getIndicator(lv_obj_t*) {
    if (system_bars_get_layout() != SYSTEM_BARS_LAYOUT_SIDE) {
        return LVGL_TILE_GRID_INDICATOR_BELOW;
    }
    // The dots are on the inner side of the strip
    return system_bars_get_alignment() == SYSTEM_BARS_ALIGNMENT_RIGHT ? LVGL_TILE_GRID_INDICATOR_RIGHT : LVGL_TILE_GRID_INDICATOR_LEFT;
}

bool isVerticalSwipe(lv_obj_t*) {
    // The page buttons are up and down in the side layout
    return system_bars_get_layout() == SYSTEM_BARS_LAYOUT_SIDE;
}

bool ignoreSwipe(lv_obj_t*) {
    // A swipe down from the top of the screen opens the system menu
    return system_bars_get_layout() == SYSTEM_BARS_LAYOUT_SIDE && system_bars_is_menu_swipe_zone_pressed();
}

void onSystemPreviousPage(void* userData) {
    lvgl_tile_grid_previous_page(static_cast<lv_obj_t*>(userData));
}

void onSystemNextPage(void* userData) {
    lvgl_tile_grid_next_page(static_cast<lv_obj_t*>(userData));
}

/** The grid that the page buttons change. Only used with the LVGL lock held. */
lv_obj_t* buttonsGrid = nullptr;

void onButtonsGridDeleted(lv_event_t* event) {
    if (buttonsGrid == lv_event_get_target_obj(event)) {
        buttonsGrid = nullptr;
        system_bars_set_page_button_callbacks(nullptr, nullptr, nullptr);
        system_bars_set_page_buttons_visible(false);
    }
}

void onPagingVisible(lv_obj_t* grid, bool visible) {
    // The buttons follow the grid that was built last, e.g. when another window's grid used them before
    if (buttonsGrid != grid) {
        buttonsGrid = grid;
        lv_obj_add_event_cb(grid, onButtonsGridDeleted, LV_EVENT_DELETE, nullptr);
        system_bars_set_page_button_callbacks(onSystemPreviousPage, onSystemNextPage, grid);
    }
    system_bars_set_page_buttons_visible(visible);
}

const LvglTileGridPaging SYSTEM_BARS_PAGING = {
    .get_indicator = getIndicator,
    .is_vertical_swipe = isVerticalSwipe,
    .ignore_swipe = ignoreSwipe,
    .on_paging_visible = onPagingVisible,
};

} // namespace

void addTileGridItems(lv_obj_t* grid, const std::vector<TileGridItem>& items) {
    for (const auto& item : items) {
        const LvglTileGridItem gridItem = {
            .id = item.id.c_str(),
            .name = item.name.c_str(),
            .icon = item.icon,
            .highlighted = item.highlighted,
            .image = item.image,
            .image_recolor = item.imageRecolor,
            .user_data = nullptr,
        };
        lvgl_tile_grid_add_item(grid, &gridItem);
    }
}

const LvglTileGridPaging* getSystemBarsPaging() {
    return &SYSTEM_BARS_PAGING;
}

}
