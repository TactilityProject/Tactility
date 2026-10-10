#include <Tactility/app/TileGrid.h>

#include <lvgl/devices/indev.h>
#include <lvgl/fonts.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/insets.h>
#include <lvgl/theme.h>
#include <lvgl/widgets/badge.h>
#include <lvgl/widgets/icon_button.h>
#include <lvgl/widgets/page_indicator.h>

#include <algorithm>
#include <string>

namespace tt::app {

namespace {

constexpr int32_t TILE_GAP = 6;
// Minimum label width, as a multiple of the label's font size
constexpr uint32_t MIN_LABEL_WIDTH_IN_FONT_SIZES = 6;

struct PageLayout {
    int32_t iconSize;
    int32_t pad;
    int32_t tileWidth;
    int32_t tileHeight;
    int32_t topOffset;
    uint32_t pageSize;
};

PageLayout computePageLayout(lv_obj_t* grid) {
    const auto icon_size = static_cast<int32_t>(lvgl_get_shared_icon_large_font_height());
    const auto pad = icon_size / 16;
    const auto gap = TILE_GAP;
    const auto text_height = lv_font_get_line_height(lvgl_get_text_font(FONT_SIZE_SMALL));
    // The label needs room for a few characters, also when the icon font is smaller (e.g. a fallback)
    const auto text_min_width = static_cast<int32_t>(MIN_LABEL_WIDTH_IN_FONT_SIZES * lvgl_get_text_font_height(FONT_SIZE_SMALL));
    const auto tile_min_width = std::max<int32_t>(2 * icon_size, text_min_width) + 2 * pad;
    const auto tile_height = 2 * pad + icon_size + pad / 2 + text_height;
    const auto available_width = lv_obj_get_content_width(grid);
    const auto available_height = lv_obj_get_content_height(grid);
    const auto columns = std::max<int32_t>(1, (available_width + gap) / (tile_min_width + gap));
    const auto rows = std::max<int32_t>(1, (available_height + gap) / (tile_height + gap));
    return {
        .iconSize = icon_size,
        .pad = pad,
        .tileWidth = std::max<int32_t>(tile_min_width, (available_width - (columns - 1) * gap) / columns),
        .tileHeight = tile_height,
        .topOffset = std::max<int32_t>(0, (available_height - (rows * tile_height + (rows - 1) * gap)) / 2),
        .pageSize = static_cast<uint32_t>(columns * rows)
    };
}

const TileGridItem* getTileItem(lv_event_t* e) {
    return static_cast<const TileGridItem*>(lv_obj_get_user_data(lv_event_get_target_obj(e)));
}


// The page indicator gets the space that the buttons leave.
// It's the only child that is neither clickable nor the spacer (which grows).
void updatePageIndicatorWidth(void* data) {
    auto* bottom_bar = static_cast<lv_obj_t*>(data);
    lv_obj_t* indicator = nullptr;
    int32_t buttons_width = 0;
    int32_t gap_count = -1;
    const uint32_t count = lv_obj_get_child_count(bottom_bar);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t* child = lv_obj_get_child(bottom_bar, static_cast<int32_t>(i));
        if (lv_obj_get_style_flex_grow(child, LV_PART_MAIN) > 0) {
            gap_count++;
        } else if (!lv_obj_is_clickable(child)) {
            indicator = child;
            gap_count++;
        } else if (!lv_obj_is_hidden(child)) {
            buttons_width += lv_obj_get_width(child);
            gap_count++;
        }
    }
    if (indicator == nullptr) {
        return;
    }
    const int32_t gaps = gap_count * lv_obj_get_style_pad_column(bottom_bar, LV_PART_MAIN);
    const int32_t margins = lv_obj_get_style_margin_left(indicator, LV_PART_MAIN) + lv_obj_get_style_margin_right(indicator, LV_PART_MAIN);
    const int32_t available = lv_obj_get_content_width(bottom_bar) - buttons_width - gaps - margins;
    const int32_t max_width = std::max<int32_t>(0, available);
    // Setting a style marks the layout as dirty, which would resize the bar and call this again
    if (lv_obj_get_style_max_width(indicator, LV_PART_MAIN) != max_width) {
        lv_obj_set_style_max_width(indicator, max_width, LV_STATE_DEFAULT);
    }
}

void onBottomBarSizeChanged(lv_event_t* event) {
    lv_async_call_cancel(updatePageIndicatorWidth, lv_event_get_target_obj(event));
    lv_async_call(updatePageIndicatorWidth, lv_event_get_target_obj(event));
}

// Puts the bar before or after the grid in their input group, without moving the focus
void setGroupOrder(lv_obj_t* grid, lv_obj_t* bar, bool barFirst) {
    lv_group_t* group = lv_obj_get_group(bar);
    if (group == nullptr || lv_obj_get_group(grid) != group) {
        return;
    }
    int32_t grid_index = -1;
    int32_t bar_index = -1;
    const uint32_t count = lv_group_get_obj_count(group);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t* obj = lv_group_get_obj_by_index(group, i);
        if (obj == grid) {
            grid_index = static_cast<int32_t>(i);
        } else if (obj == bar) {
            bar_index = static_cast<int32_t>(i);
        }
    }
    if ((bar_index < grid_index) == barFirst) {
        return;
    }
    // Swapping also swaps the focus
    lv_obj_t* focused = lv_group_get_focused(group);
    lv_group_swap_obj(grid, bar);
    if (focused != nullptr && lv_group_get_focused(group) != focused) {
        lv_group_focus_obj(focused);
    }
}

lv_obj_t* findFirstBarButton(lv_obj_t* bar) {
    const uint32_t count = lv_obj_get_child_count(bar);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t* child = lv_obj_get_child(bar, static_cast<int32_t>(i));
        if (!lv_obj_is_hidden(child) && lv_obj_is_clickable(child)) {
            return child;
        }
    }
    return nullptr;
}

void onBottomBarDeleted(lv_event_t* event) {
    lv_async_call_cancel(updatePageIndicatorWidth, lv_event_get_target_obj(event));
}

} // namespace

void TileGrid::onDeferredNextPage(void* userData) {
    static_cast<TileGrid*>(userData)->goToNextPage();
}

void TileGrid::onDeferredPreviousPage(void* userData) {
    static_cast<TileGrid*>(userData)->goToPreviousPage();
}

void TileGrid::onDeferredRepopulate(void* userData) {
    static_cast<TileGrid*>(userData)->populate();
}

// A layout pass can resize the grid temporarily (e.g. to its content height, before the parent's flex layout grows it again)
void TileGrid::onDeferredResize(void* userData) {
    auto* self = static_cast<TileGrid*>(userData);
    if (lv_obj_get_width(self->grid) != self->populatedWidth || lv_obj_get_height(self->grid) != self->populatedHeight) {
        self->populate();
    }
}

void TileGrid::onGridDeleted(lv_event_t* e) {
    // Cancels a still-pending repopulate scheduled right before the grid itself was deleted (e.g. the app closing)
    lv_async_call_cancel(onDeferredRepopulate, lv_event_get_user_data(e));
    lv_async_call_cancel(onDeferredResize, lv_event_get_user_data(e));
    lv_async_call_cancel(onDeferredNextPage, lv_event_get_user_data(e));
    lv_async_call_cancel(onDeferredPreviousPage, lv_event_get_user_data(e));
}

// Deferred: page size depends on the grid size, which is final only after the layout pass that triggered this.
void TileGrid::onGridSizeChanged(lv_event_t* e) {
    // One pending check is enough, also when the size changes several times before it runs
    lv_async_call_cancel(onDeferredResize, lv_event_get_user_data(e));
    lv_async_call(onDeferredResize, lv_event_get_user_data(e));
}

// Changing the page doesn't keep the selected app selected: that would return to the selected app's page
void TileGrid::goToPreviousPage() {
    page = page > 0 ? page - 1 : pageCount - 1;
    keepSelectionOnRepopulate = false;
    populate();
    if (callbacks.onPageChanged != nullptr) {
        callbacks.onPageChanged(callbacks.userData);
    }
}

void TileGrid::goToNextPage() {
    page = page + 1 < pageCount ? page + 1 : 0;
    keepSelectionOnRepopulate = false;
    populate();
    if (callbacks.onPageChanged != nullptr) {
        callbacks.onPageChanged(callbacks.userData);
    }
}

void TileGrid::onPrevPressed(lv_event_t* e) {
    static_cast<TileGrid*>(lv_event_get_user_data(e))->goToPreviousPage();
}

void TileGrid::onNextPressed(lv_event_t* e) {
    static_cast<TileGrid*>(lv_event_get_user_data(e))->goToNextPage();
}

// Swipes that start on a tile reach the grid too, as tiles pass their gestures on to the grid
void TileGrid::onGridGesture(lv_event_t* e) {
    auto* self = static_cast<TileGrid*>(lv_event_get_user_data(e));
    lv_indev_t* indev = lv_indev_active();
    if (indev == nullptr || self->pageCount <= 1) {
        return;
    }
    const lv_dir_t direction = lv_indev_get_gesture_dir(indev);
    if (direction != LV_DIR_LEFT && direction != LV_DIR_RIGHT) {
        return;
    }
    // Keeps the tile where the swipe started from being clicked when the finger is lifted
    lv_indev_wait_release(indev);
    // Deferred: changing the page deletes the tiles, possibly including the one this event came from
    lv_async_call(direction == LV_DIR_LEFT ? onDeferredNextPage : onDeferredPreviousPage, self);
}

void TileGrid::onTileClicked(lv_event_t* e) {
    const auto* self = static_cast<TileGrid*>(lv_event_get_user_data(e));
    self->callbacks.onClicked(*getTileItem(e), self->callbacks.userData);
}

void TileGrid::onTileLongPressed(lv_event_t* e) {
    const auto* self = static_cast<TileGrid*>(lv_event_get_user_data(e));
    self->callbacks.onLongPressed(*getTileItem(e), self->callbacks.userData);
}

void TileGrid::onTileKey(lv_event_t* e) {
    const auto* self = static_cast<TileGrid*>(lv_event_get_user_data(e));
    self->callbacks.onKey(*getTileItem(e), lv_event_get_key(e), self->callbacks.userData);
}

void TileGrid::requestRepopulate(bool keepSelection) {
    keepSelectionOnRepopulate = keepSelection;
    lv_async_call(onDeferredRepopulate, this);
}

void TileGrid::populate() {
    // Captured before lv_obj_clean() deletes the currently focused tile below: creating
    // replacement tiles doesn't restore the focus, so the item id is remembered here and
    // re-focused on its new tile once rebuilt.
    std::string focusedId;
    const bool keepSelection = keepSelectionOnRepopulate;
    lv_group_t* group = lv_group_get_default();
    if (group != nullptr && keepSelection) {
        // Grid navigation focuses the grid in the group, and a tile within the grid
        lv_obj_t* focused = lv_group_get_focused(group) == grid ? lvgl_grid_navigation_get_focused(grid) : nullptr;
        if (focused != nullptr && lv_obj_get_parent(focused) == grid) {
            focusedId = static_cast<const TileGridItem*>(lv_obj_get_user_data(focused))->id;
        }
    }
    keepSelectionOnRepopulate = true;

    // Old tiles hold pointers into items, so they are deleted before items is replaced.
    lv_obj_clean(grid);
    items = callbacks.collect(callbacks.userData);

    // Centers a full page of rows, so a partially filled last page keeps its rows in the same positions.
    lv_obj_set_style_pad_top(grid, 0, LV_STATE_DEFAULT);
    const PageLayout layout = computePageLayout(grid);
    populatedWidth = lv_obj_get_width(grid);
    populatedHeight = lv_obj_get_height(grid);
    lv_obj_set_style_pad_top(grid, layout.topOffset, LV_STATE_DEFAULT);

    const uint32_t item_count = items.size();
    const uint32_t page_count = std::max<uint32_t>(1, (item_count + layout.pageSize - 1) / layout.pageSize);
    // Follows the focused item onto its new page, e.g. after a reorder moved it.
    if (!focusedId.empty()) {
        const auto it = std::ranges::find_if(items, [&](const TileGridItem& item) {
            return focusedId == item.id;
        });
        if (it != items.end()) {
            page = static_cast<uint32_t>(it - items.begin()) / layout.pageSize;
        }
    }
    page = std::min(page, page_count - 1);
    pageCount = page_count;

    if (pageIndicator != nullptr) {
        lv_obj_set_hidden(pageIndicator, page_count <= 1);
        lvgl_page_indicator_set_page_count(pageIndicator, page_count);
        lvgl_page_indicator_set_page(pageIndicator, page);
    }
    if (prevButton != nullptr) {
        lv_obj_set_hidden(prevButton, page_count <= 1);
        lv_obj_set_hidden(nextButton, page_count <= 1);
    }

    lv_obj_t* focusedTile = nullptr;
    const uint32_t first = page * layout.pageSize;
    const uint32_t last = std::min(item_count, first + layout.pageSize);
    for (uint32_t i = first; i < last; i++) {
        const auto& item = items[i];

        // The theme styles the tile as an icon button, including its focus and pressed states
        lv_obj_t* tile = lvgl_icon_button_create(grid);
        lv_obj_set_size(tile, layout.tileWidth, layout.tileHeight);
        lv_obj_set_style_pad_all(tile, layout.pad, LV_STATE_DEFAULT);
        lv_obj_set_style_pad_row(tile, layout.pad / 2, LV_STATE_DEFAULT);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_user_data(tile, const_cast<TileGridItem*>(&item));

        lv_obj_t* icon = lv_label_create(tile);
        lv_obj_set_style_text_font(icon, lvgl_get_shared_icon_large_font(), LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
        lv_obj_set_size(icon, layout.iconSize, layout.iconSize);
        const char* icon_text = item.icon;
        if (icon_text == nullptr && callbacks.resolveIcon != nullptr) {
            icon_text = callbacks.resolveIcon(item, callbacks.userData);
        }
        lv_label_set_text(icon, icon_text != nullptr ? icon_text : "");
        if (iconColor == IconColor::Primary) {
            lv_obj_set_style_text_color(icon, lv_theme_get_color_primary(icon), LV_STATE_DEFAULT);
        } else if (iconColor == IconColor::Secondary) {
            lv_obj_set_style_text_color(icon, lv_theme_get_color_secondary(icon), LV_STATE_DEFAULT);
        }

        lv_obj_t* label = lv_label_create(tile);
        lv_obj_set_style_text_font(label, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
        // Dots mode needs a fixed height, otherwise the label wraps instead of truncating
        lv_obj_set_size(label, LV_PCT(100), lv_font_get_line_height(lvgl_get_text_font(FONT_SIZE_SMALL)));
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text(label, item.name.c_str());

        // A badge marks favourites, as it doesn't look like the focus indicator
        if (item.highlighted) {
            auto* badge = lvgl_badge_create(tile);
            lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, 0, 0);
        }

        lv_obj_add_event_cb(tile, onTileClicked, LV_EVENT_SHORT_CLICKED, this);
        if (callbacks.onLongPressed != nullptr) {
            lv_obj_add_event_cb(tile, onTileLongPressed, LV_EVENT_LONG_PRESSED, this);
        }
        if (callbacks.onKey != nullptr) {
            lv_obj_add_event_cb(tile, onTileKey, LV_EVENT_KEY, this);
        }

        if (!focusedId.empty() && focusedId == item.id) {
            focusedTile = tile;
        }
    }

    if (focusedTile != nullptr) {
        lv_group_focus_obj(grid);
        lv_gridnav_set_focused(grid, focusedTile, LV_ANIM_OFF);
    } else if (!keepSelection && group != nullptr && lv_group_get_focused(group) == grid) {
        // Grid navigation selects the first new tile when the grid is focused, but a page change shows no selection.
        // The next key that moves the focus selects a tile again.
        lvgl_focus_hide_key_selection(group);
    }
}

void TileGrid::createGrid(lv_obj_t* parent) {
    grid = lv_obj_create(parent);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_all(grid, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(grid, TILE_GAP, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(grid, TILE_GAP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(grid, 0, LV_STATE_DEFAULT);
    lv_obj_set_scrollable(grid, false);
    // The arrow keys move between the tiles in rows and columns
    lvgl_grid_navigation_add(grid);
    lv_obj_add_event_cb(grid, onGridDeleted, LV_EVENT_DELETE, this);
    if (swipeNavigation) {
        // LVGL sends a gesture to the first object, starting at the touched one, that doesn't pass it on to its parent
        lv_obj_set_gesture_bubble(grid, false);
        lv_obj_add_event_cb(grid, onGridGesture, LV_EVENT_GESTURE, this);
    }
}

// Keys enter the bar on the previous page button, or on the first visible button when there's only one page
void TileGrid::onBottomBarFocused(lv_event_t* e) {
    const auto* self = static_cast<TileGrid*>(lv_event_get_user_data(e));
    lv_obj_t* current = lvgl_grid_navigation_get_focused(self->bottomBar);
    lv_obj_t* target = lv_obj_is_hidden(self->prevButton) ? findFirstBarButton(self->bottomBar) : self->prevButton;
    if (current == nullptr || target == nullptr || target == current) {
        return;
    }
    // Only keys show the selection, the bar's previously focused child can be stale
    lv_indev_t* indev = lv_indev_active();
    const lv_indev_type_t type = indev != nullptr ? lv_indev_get_type(indev) : LV_INDEV_TYPE_NONE;
    const bool showSelection = type == LV_INDEV_TYPE_KEYPAD || type == LV_INDEV_TYPE_ENCODER;
    lv_gridnav_set_focused(self->bottomBar, target, LV_ANIM_OFF);
    if (!showSelection) {
        lv_obj_remove_state(target, LV_STATE_FOCUS_KEY);
    }
}

void TileGrid::createWidgetsWithBottomBar(lv_obj_t* parent) {
    barParent = parent;
    barSideContainer = nullptr;
    barButtons.clear();
    bottomBar = lv_obj_create(parent);
    lv_obj_set_size(bottomBar, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(bottomBar, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bottomBar, 0, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(bottomBar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bottomBar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(bottomBar, false);
    lvgl_obj_add_edge_padding(bottomBar);
    lv_obj_add_event_cb(bottomBar, onBottomBarSizeChanged, LV_EVENT_SIZE_CHANGED, nullptr);
    lv_obj_add_event_cb(bottomBar, onBottomBarDeleted, LV_EVENT_DELETE, nullptr);

    createGrid(parent);
    lv_obj_move_foreground(bottomBar);

    pageIndicator = lvgl_page_indicator_create(bottomBar);
    // Keeps the page indicator away from the screen edge
    lv_obj_set_style_margin_hor(pageIndicator, lv_obj_get_style_pad_column(bottomBar, LV_PART_MAIN), LV_STATE_DEFAULT);
    // The spacer pushes the buttons and the page indicator to opposite sides
    barSpacer = lv_obj_create(bottomBar);
    lv_obj_set_size(barSpacer, 0, 0);
    lv_obj_set_flex_grow(barSpacer, 1);
    lv_obj_set_clickable(barSpacer, false);
    prevButton = lvgl_icon_button_create(bottomBar);
    lv_label_set_text(lv_label_create(prevButton), "<");
    lv_obj_add_event_cb(prevButton, onPrevPressed, LV_EVENT_SHORT_CLICKED, this);
    nextButton = lvgl_icon_button_create(bottomBar);
    lv_label_set_text(lv_label_create(nextButton), ">");
    lv_obj_add_event_cb(nextButton, onNextPressed, LV_EVENT_SHORT_CLICKED, this);
    arrangeBar();
    // The arrow keys move between all buttons of the bar as one row
    lvgl_grid_navigation_add(bottomBar);
    lv_obj_add_event_cb(bottomBar, onBottomBarFocused, LV_EVENT_FOCUSED, this);

    finishCreate(parent);
}

void TileGrid::createWidgets(lv_obj_t* parent) {
    // Widgets of a previous build are deleted by now
    prevButton = nullptr;
    nextButton = nullptr;
    pageIndicator = nullptr;
    bottomBar = nullptr;
    barSpacer = nullptr;
    barParent = nullptr;
    barSideContainer = nullptr;
    barButtons.clear();
    createGrid(parent);
    finishCreate(parent);
}

void TileGrid::finishCreate(lv_obj_t* parent) {
    // Resolves the grid's flex_grow height, which the page size is derived from.
    lv_obj_update_layout(parent);
    lv_obj_add_event_cb(grid, onGridSizeChanged, LV_EVENT_SIZE_CHANGED, this);

    populate();

    if (!lvgl_indev_exists(LV_INDEV_TYPE_POINTER) && lv_obj_get_child_count(grid) > 0) {
        lv_group_focus_obj(grid);
        lv_gridnav_set_focused(grid, lv_obj_get_child(grid, 0), LV_ANIM_OFF);
    }
}

void TileGrid::setPageButtons(lv_obj_t* previous, lv_obj_t* next) {
    prevButton = previous;
    nextButton = next;
    lv_obj_add_event_cb(prevButton, onPrevPressed, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_add_event_cb(nextButton, onNextPressed, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_set_hidden(prevButton, pageCount <= 1);
    lv_obj_set_hidden(nextButton, pageCount <= 1);
}

lv_obj_t* TileGrid::findTile(const std::string& id) const {
    const uint32_t count = lv_obj_get_child_count(grid);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t* tile = lv_obj_get_child(grid, static_cast<int32_t>(i));
        if (static_cast<const TileGridItem*>(lv_obj_get_user_data(tile))->id == id) {
            return tile;
        }
    }
    return nullptr;
}

lv_obj_t* TileGrid::addBarButton(const char* icon, lv_event_cb_t onClicked, void* userData) {
    auto* button = lvgl_icon_button_create(bottomBar);
    auto* label = lv_label_create(button);
    lv_obj_set_style_text_font(label, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
    lv_label_set_text(label, icon);
    lv_obj_add_event_cb(button, onClicked, LV_EVENT_SHORT_CLICKED, userData);
    barButtons.push_back(button);
    arrangeBar();
    return button;
}

void TileGrid::arrangeBar() {
    // The order of the bar's children, from the start of the row or column
    std::vector<lv_obj_t*> order;
    if (barSideContainer != nullptr) {
        // The page indicator is below the grid and the spacer isn't used. The bar's own buttons are at the bottom.
        order.insert(order.end(), { barSpacer, nextButton, prevButton });
        order.insert(order.end(), barButtons.rbegin(), barButtons.rend());
    } else {
        order.insert(order.end(), barButtons.begin(), barButtons.end());
        order.insert(order.end(), { prevButton, nextButton, barSpacer, pageIndicator });
    }
    for (size_t i = 0; i < order.size(); i++) {
        lv_obj_move_to_index(order[i], static_cast<int32_t>(i));
    }
}

void TileGrid::setBarSideContainer(lv_obj_t* sideContainer) {
    if (bottomBar == nullptr || sideContainer == barSideContainer) {
        return;
    }
    barSideContainer = sideContainer;
    if (sideContainer != nullptr) {
        lv_obj_set_parent(bottomBar, sideContainer);
        lv_obj_set_size(bottomBar, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(bottomBar, LV_FLEX_FLOW_COLUMN);
        // The strip has its own background
        lv_obj_set_style_bg_opa(bottomBar, LV_OPA_TRANSP, LV_STATE_DEFAULT);
        // The buttons are close together, like the statusbar's icons above them
        lv_obj_set_style_pad_row(bottomBar, 2, LV_STATE_DEFAULT);
        lv_obj_set_hidden(barSpacer, true);
        // The page indicator is centered below the grid
        lv_obj_set_parent(pageIndicator, barParent);
        lv_obj_move_to_index(pageIndicator, lv_obj_get_index(grid) + 1);
        lv_obj_set_width(pageIndicator, LV_PCT(100));
        lv_obj_remove_local_style_prop(pageIndicator, LV_STYLE_MAX_WIDTH, LV_PART_MAIN);
        // Keeps the dots off the screen's bottom edge
        lv_obj_set_style_margin_bottom(pageIndicator, lvgl_theme_is_compact() ? 1 : 2, LV_STATE_DEFAULT);
    } else {
        lv_obj_set_parent(bottomBar, barParent);
        lv_obj_move_to_index(bottomBar, lv_obj_get_index(grid) + 1);
        lv_obj_set_size(bottomBar, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(bottomBar, LV_FLEX_FLOW_ROW);
        lv_obj_remove_local_style_prop(bottomBar, LV_STYLE_BG_OPA, LV_PART_MAIN);
        lv_obj_remove_local_style_prop(bottomBar, LV_STYLE_PAD_ROW, LV_PART_MAIN);
        lv_obj_set_hidden(barSpacer, false);
        lv_obj_set_parent(pageIndicator, bottomBar);
        lv_obj_set_width(pageIndicator, LV_SIZE_CONTENT);
        lv_obj_remove_local_style_prop(pageIndicator, LV_STYLE_MARGIN_BOTTOM, LV_PART_MAIN);
    }
    // The keys move through the bar where it's shown: below the statusbar's icons in the side strip, or below the grid
    setGroupOrder(grid, bottomBar, sideContainer != nullptr);
    if (sideContainer != nullptr) {
        // Runs before grid navigation, which would move the focus along the group instead of sideways
        lv_obj_add_event_cb(bottomBar, onSideBarKey, static_cast<lv_event_code_t>(LV_EVENT_KEY | LV_EVENT_PREPROCESS), this);
    } else {
        lv_obj_remove_event_cb(bottomBar, onSideBarKey);
    }
    arrangeBar();
}

// In the side strip on the left, the right key moves the focus to the grid, and the left key stays at the screen's edge
void TileGrid::onSideBarKey(lv_event_t* e) {
    auto* self = static_cast<TileGrid*>(lv_event_get_user_data(e));
    const uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_RIGHT) {
        lv_group_focus_obj(self->grid);
        lv_event_stop_processing(e);
    } else if (key == LV_KEY_LEFT) {
        lv_event_stop_processing(e);
    }
}

void TileGrid::destroyWidgets() {
    // The window only deletes its own widgets, and the bar can be in the side strip
    if (bottomBar != nullptr && barSideContainer != nullptr) {
        lv_obj_delete(bottomBar);
    }
    // The other bar widgets are deleted with the bar or the window, so no pointer may outlive them
    bottomBar = nullptr;
    barSideContainer = nullptr;
    barParent = nullptr;
    pageIndicator = nullptr;
    prevButton = nullptr;
    nextButton = nullptr;
    barSpacer = nullptr;
    barButtons.clear();
}

}
