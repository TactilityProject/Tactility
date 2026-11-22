#include <Tactility/app/TileGrid.h>

#include <lvgl/devices/indev.h>
#include <lvgl/fonts.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/theme.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/icon_button.h>
#include <lvgl/widgets/page_indicator.h>

#include <lvgl_window_manager/system_bars.h>

#include <tactility/memory.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace tt::app {

bool areSvgIconsEnabled() {
#ifdef ESP_PLATFORM
    return memory_external_total() > 0;
#else
    return true;
#endif
}

uint32_t getTileIconSize() {
    const float scale = lvgl_theme_get_launcher_icon_size_scale() / 100.0f;
    return static_cast<uint32_t>(::std::lround(lvgl_get_text_font_height(FONT_SIZE_DEFAULT) * 2.5f * scale));
}

uint32_t getCardIconSize() {
    return static_cast<uint32_t>(lvgl_get_text_font_height(FONT_SIZE_DEFAULT));
}

namespace {

// Minimum label width, as a multiple of the label's font size
constexpr uint32_t MIN_LABEL_WIDTH_IN_FONT_SIZES = 6;
// Reference width of a card, as a multiple of the default font size. Cards grow to fill their row.
constexpr uint32_t CARD_WIDTH_IN_FONT_SIZES = 12;

// Scales a spacing in pixels with the device's launcher spacing scale
int32_t scaleSpacing(int32_t spacing) {
    const float scale = lvgl_theme_get_launcher_icon_spacing_scale() / 100.0f;
    return static_cast<int32_t>(::std::lround(spacing * scale));
}

// The room outside a card for its selection ring: the ring and its offset
int32_t getCardRingSpace() {
    const int32_t ringWidth = lvgl_theme_lerp(1, LV_DPX(2), lvgl_theme_get_size()) + LV_DPX(1);
    return ringWidth + lvgl_theme_lerp(1, LV_DPX(2), lvgl_theme_get_size());
}

// Spacing between tiles, horizontally and vertically. Cards keep twice the room of their selection ring, however small the scale is.
int32_t getTileGap(bool card) {
    const int32_t gap = scaleSpacing(lvgl_theme_lerp(0, LV_DPX(12), lvgl_theme_get_spacing()));
    return card ? std::max(gap, 2 * getCardRingSpace()) : gap;
}

// The padding of a card, which is also the space between its icon and name. It is at least 2 dp, however small the scale is.
int32_t getCardPad() {
    return std::max<int32_t>(LV_DPX(2), scaleSpacing(lvgl_theme_lerp(LV_DPX(4), LV_DPX(16), lvgl_theme_get_spacing())));
}

struct PageLayout {
    int32_t iconSize;
    int32_t pad;
    int32_t framePad;
    int32_t labelGap;
    int32_t tileWidth;
    int32_t tileHeight;
    /** The space between the columns */
    int32_t horizontalSpacing;
    /** The space between the rows */
    int32_t verticalSpacing;
    /** The space left of the columns and above the rows, which center the tiles in the grid */
    int32_t leftMargin;
    int32_t topMargin;
    int32_t columns;
    int32_t rows;
    uint32_t pageSize;
};

// The padding around the column of page dots, which is also the padding below a row of dots
int32_t getPageIndicatorPad() {
    return lvgl_theme_lerp(LV_DPX(2), LV_DPX(8), lvgl_theme_get_spacing());
}

// The width of a column of page dots with its padding. The dot size is the one of the theme.
int32_t getPageIndicatorColumnWidth() {
    return lvgl_theme_lerp(6, LV_DPX(8), lvgl_theme_get_size()) + 2 * getPageIndicatorPad();
}

// The height of a row of page dots with the padding below it
int32_t getPageIndicatorRowHeight() {
    return lvgl_theme_lerp(6, LV_DPX(8), lvgl_theme_get_size()) + getPageIndicatorPad();
}

PageLayout computePageLayout(lv_obj_t* grid, TileGrid::Style style, int32_t reservedWidth, int32_t reservedHeight) {
    const bool card = style == TileGrid::Style::Card;
    const auto icon_size = static_cast<int32_t>(card ? getCardIconSize() : getTileIconSize());
    const auto pad = card ? getCardPad() : icon_size / 16;
    // The focus ring is drawn inside the frame, at most 3dp wide, and leaves 1dp to the icon.
    // A card shows its focus ring itself.
    const auto frame_pad = card ? 0 : LV_DPX(3) + LV_DPX(1);
    const auto label_gap = lvgl_theme_lerp(LV_DPX(1), LV_DPX(4), lvgl_theme_get_spacing());
    const auto gap = getTileGap(card);
    const auto text_height = lv_font_get_line_height(lvgl_get_text_font(FONT_SIZE_SMALL));
    // The label needs room for a few characters, also when the icon font is smaller (e.g. a fallback)
    const auto text_min_width = static_cast<int32_t>(MIN_LABEL_WIDTH_IN_FONT_SIZES * lvgl_get_text_font_height(FONT_SIZE_SMALL));
    const auto tile_min_width = card
        ? static_cast<int32_t>(CARD_WIDTH_IN_FONT_SIZES * lvgl_get_text_font_height(FONT_SIZE_DEFAULT))
        : std::max<int32_t>(2 * icon_size, text_min_width) + 2 * pad;
    const auto tile_height = card
        ? 2 * frame_pad + icon_size + 2 * pad
        : 2 * frame_pad + icon_size + label_gap + text_height + pad;
    // The grid is kept the gap away from its edges. Its padding is set from the offsets, so it isn't part of the measurement
    // The width that the tiles can use, without the space reserved for the page dots
    const auto grid_width = lv_obj_get_width(grid) - reservedWidth;
    const auto available_width = grid_width - 2 * gap;
    const auto grid_height = lv_obj_get_height(grid) - reservedHeight;
    const auto available_height = grid_height - 2 * gap;
    const auto columns = std::max<int32_t>(1, (available_width + gap) / (tile_min_width + gap));
    const auto rows = std::max<int32_t>(1, (available_height + gap) / (tile_height + gap));
    // A single column can be narrower than the preferred width, on displays that are narrower than that
    const auto tile_width = std::min<int32_t>(std::max<int32_t>(tile_min_width, (available_width - (columns - 1) * gap) / columns), std::max<int32_t>(1, available_width));
    return {
        .iconSize = icon_size,
        .pad = pad,
        .framePad = frame_pad,
        .labelGap = label_gap,
        .tileWidth = tile_width,
        .tileHeight = tile_height,
        // The tiles keep the gap between them, and the space that they leave is divided over the edges
        .horizontalSpacing = gap,
        .verticalSpacing = gap,
        .leftMargin = std::max<int32_t>(gap, (grid_width - columns * tile_width - (columns - 1) * gap) / 2),
        .topMargin = std::max<int32_t>(gap, (grid_height - rows * tile_height - (rows - 1) * gap) / 2),
        .columns = columns,
        .rows = rows,
        .pageSize = static_cast<uint32_t>(columns * rows)
    };
}

const TileGridItem* getTileItem(lv_event_t* e) {
    return static_cast<const TileGridItem*>(lv_obj_get_user_data(lv_event_get_target_obj(e)));
}


// The page indicator is a row centered below the grid, or a column in the space next to the strip, within the grid
void placePageIndicator(lv_obj_t* indicator, lv_obj_t* grid, bool column, bool alignRight) {
    if (column) {
        lv_obj_set_floating(indicator, true);
        lvgl_page_indicator_set_vertical(indicator, true);
        lv_obj_set_width(indicator, LV_SIZE_CONTENT);
        lv_obj_remove_local_style_prop(indicator, LV_STYLE_MARGIN_BOTTOM, LV_PART_MAIN);
        lv_obj_update_layout(indicator);
        // Centered on the grid, which can have something above it, like a toolbar.
        // The alignment is within the grid's padding, which has the tiles in it.
        const int32_t padLeft = lv_obj_get_style_pad_left(grid, LV_PART_MAIN);
        const int32_t padTop = lv_obj_get_style_pad_top(grid, LV_PART_MAIN);
        lv_obj_align_to(
            indicator, grid, alignRight ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID,
            alignRight ? -getPageIndicatorPad() : getPageIndicatorPad() - padLeft,
            -padTop / 2
        );
    } else {
        lv_obj_set_floating(indicator, false);
        // The alignment of the column must not move the row
        lv_obj_remove_local_style_prop(indicator, LV_STYLE_ALIGN, LV_PART_MAIN);
        lv_obj_remove_local_style_prop(indicator, LV_STYLE_X, LV_PART_MAIN);
        lv_obj_remove_local_style_prop(indicator, LV_STYLE_Y, LV_PART_MAIN);
        lvgl_page_indicator_set_vertical(indicator, false);
        lv_obj_set_width(indicator, LV_PCT(100));
        // Keeps the dots off the screen's bottom edge
        lv_obj_set_style_margin_bottom(indicator, getPageIndicatorPad(), LV_STATE_DEFAULT);
    }
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

    // The fallback widget belongs to a window that is going away too, so the cancelled request must not select it later
    auto* self = static_cast<TileGrid*>(lv_event_get_user_data(e));
    self->selectFirstOnRepopulate = false;
    self->emptyFallback = nullptr;
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

// Swipes that start on a tile reach the grid too, as tiles pass their gestures on to the grid
void TileGrid::onGridGesture(lv_event_t* e) {
    auto* self = static_cast<TileGrid*>(lv_event_get_user_data(e));
    lv_indev_t* indev = lv_indev_active();
    if (indev == nullptr || self->pageCount <= 1) {
        return;
    }
    const lv_dir_t direction = lv_indev_get_gesture_dir(indev);
    // Up and down in the side layout, where the page buttons are up and down too, otherwise left and right
    const bool vertical = self->verticalSwipeInSideLayout && system_bars_get_layout() == SYSTEM_BARS_LAYOUT_SIDE;
    const lv_dir_t next = vertical ? LV_DIR_TOP : LV_DIR_LEFT;
    const lv_dir_t previous = vertical ? LV_DIR_BOTTOM : LV_DIR_RIGHT;
    if (direction != next && direction != previous) {
        return;
    }
    // A swipe down from the top of the screen opens the system menu
    if (vertical && system_bars_is_menu_swipe_zone_pressed()) {
        return;
    }
    // Keeps the tile where the swipe started from being clicked when the finger is lifted
    lv_indev_wait_release(indev);
    // Deferred: changing the page deletes the tiles, possibly including the one this event came from
    lv_async_call(direction == next ? onDeferredNextPage : onDeferredPreviousPage, self);
}

void TileGrid::onTileStateChanged(lv_event_t* e) {
    constexpr auto MIRRORED = static_cast<lv_state_t>(LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY | LV_STATE_PRESSED | LV_STATE_CHECKED);
    lv_obj_t* tile = lv_event_get_target_obj(e);
    auto* frame = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    const lv_state_t state = lv_obj_get_state(tile);
    lv_obj_remove_state(frame, static_cast<lv_state_t>(MIRRORED & ~state));
    lv_obj_add_state(frame, static_cast<lv_state_t>(MIRRORED & state));
}

void TileGrid::onTilePressed(lv_event_t* e) {
    auto* self = static_cast<TileGrid*>(lv_event_get_user_data(e));
    lv_indev_t* indev = lv_indev_active();
    if (indev != nullptr && lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
        lv_indev_get_point(indev, &self->pressPoint);
    }
}

void TileGrid::onTileClicked(lv_event_t* e) {
    const auto* self = static_cast<TileGrid*>(lv_event_get_user_data(e));
    // A pointer that moved before it was released swiped. A swipe that isn't recognized as a gesture would still click.
    lv_indev_t* indev = lv_indev_active();
    if (indev != nullptr && lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
        lv_point_t point;
        lv_indev_get_point(indev, &point);
        const int32_t limit = LV_DPX(10);
        if (LV_ABS(point.x - self->pressPoint.x) > limit || LV_ABS(point.y - self->pressPoint.y) > limit) {
            return;
        }
    }
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
    // The latest request decides the selection policy
    keepSelectionOnRepopulate = keepSelection;
    selectFirstOnRepopulate = false;
    emptyFallback = nullptr;
    lv_async_call(onDeferredRepopulate, this);
}

void TileGrid::replaceEmptyFallback(lv_obj_t* fallback) {
    if (selectFirstOnRepopulate) {
        emptyFallback = fallback;
    }
}

void TileGrid::requestRepopulateSelectingFirst(lv_obj_t* fallback) {
    requestRepopulate(false);
    selectFirstOnRepopulate = true;
    emptyFallback = fallback;
}

bool TileGrid::isPagingVisible() const {
    return pageIndicator != nullptr && !lv_obj_is_hidden(pageIndicator);
}

void TileGrid::setPagingVisible(bool visible) {
    if (pageIndicator != nullptr) {
        lv_obj_set_hidden(pageIndicator, !visible);
    }
    if (systemPageButtons) {
        system_bars_set_page_buttons_visible(visible);
    }
}

void TileGrid::onSystemPreviousPage(void* userData) {
    static_cast<TileGrid*>(userData)->goToPreviousPage();
}

void TileGrid::onSystemNextPage(void* userData) {
    static_cast<TileGrid*>(userData)->goToNextPage();
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
    const bool selectFirst = selectFirstOnRepopulate;
    lv_obj_t* fallback = emptyFallback;
    selectFirstOnRepopulate = false;
    emptyFallback = nullptr;

    // Old tiles hold pointers into items, so they are deleted before items is replaced.
    lv_obj_clean(grid);
    if (callbacks.onTilesCleared != nullptr) {
        callbacks.onTilesCleared(callbacks.userData);
    }
    items = callbacks.collect(callbacks.userData);

    // The spacing is computed for a full page, so a partially filled last page keeps its rows in the same positions.
    lv_obj_set_style_pad_top(grid, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(grid, 0, LV_STATE_DEFAULT);
    // In the side layout, the page dots are a column next to the strip, which the tiles leave free
    const bool dotsInColumn = sideIndicator && system_bars_get_layout() == SYSTEM_BARS_LAYOUT_SIDE;
    const bool dotsRight = system_bars_get_alignment() == SYSTEM_BARS_ALIGNMENT_RIGHT;
    // The same space is left free on the other side, so the tiles stay centered
    const int32_t columnWidth = dotsInColumn ? getPageIndicatorColumnWidth() : 0;
    const int32_t reservedWidth = 2 * columnWidth;
    // A row of dots below the grid is balanced by the same space above the tiles
    const bool dotsBelow = sideIndicator && !dotsInColumn;
    auto getReservedHeight = [&] { return dotsBelow && isPagingVisible() ? getPageIndicatorRowHeight() : 0; };
    PageLayout layout = computePageLayout(grid, style, reservedWidth, getReservedHeight());
    const uint32_t item_count = items.size();
    uint32_t page_count = std::max<uint32_t>(1, (item_count + layout.pageSize - 1) / layout.pageSize);

    // The page indicator and buttons take room from the grid, and are only shown with multiple pages.
    // Showing them changes the grid size and so the page size, so the layout is computed again with them in place.
    const bool wasPagingVisible = isPagingVisible();
    setPagingVisible(page_count > 1);
    if (wasPagingVisible != (page_count > 1)) {
        lv_obj_update_layout(lv_obj_get_parent(grid));
        layout = computePageLayout(grid, style, reservedWidth, getReservedHeight());
        page_count = std::max<uint32_t>(1, (item_count + layout.pageSize - 1) / layout.pageSize);
        setPagingVisible(page_count > 1);
    }
    populatedWidth = lv_obj_get_width(grid);
    populatedHeight = lv_obj_get_height(grid);
    lv_obj_set_style_pad_row(grid, layout.verticalSpacing, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(grid, layout.horizontalSpacing, LV_STATE_DEFAULT);

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

    // The rows of a page of cards that isn't full are centered vertically
    int32_t top = layout.topMargin;
    if (style == Style::Card) {
        const auto count = static_cast<int32_t>(std::min<uint32_t>(item_count - page * layout.pageSize, layout.pageSize));
        const auto used_rows = (count + layout.columns - 1) / layout.columns;
        top += (layout.rows - used_rows) * (layout.tileHeight + layout.verticalSpacing) / 2;
    }
    lv_obj_set_style_pad_top(grid, top + getReservedHeight(), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(grid, layout.leftMargin + columnWidth, LV_STATE_DEFAULT);

    if (pageIndicator != nullptr) {
        lvgl_page_indicator_set_page_count(pageIndicator, page_count);
        lvgl_page_indicator_set_page(pageIndicator, page);
    }
    if (pageIndicator != nullptr) {
        placePageIndicator(pageIndicator, grid, dotsInColumn, dotsRight);
    }
    lv_obj_t* focusedTile = nullptr;
    const uint32_t first = page * layout.pageSize;
    const uint32_t last = std::min(item_count, first + layout.pageSize);
    for (uint32_t i = first; i < last; i++) {
        const auto& item = items[i];

        // The tile receives the input. A tile's icon frame shows the focus, pressed and checked states of the theme's icon button,
        // and a card shows them itself.
        const bool card = style == Style::Card;
        lv_obj_t* tile;
        if (card) {
            tile = lvgl_card_create(grid);
            lv_obj_set_size(tile, layout.tileWidth, layout.tileHeight);
            lv_obj_set_style_pad_all(tile, layout.pad, LV_STATE_DEFAULT);
            lv_obj_set_style_pad_column(tile, layout.pad, LV_STATE_DEFAULT);
            lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        } else {
            tile = lv_obj_create(grid);
            lv_obj_remove_style_all(tile);
            lv_obj_set_size(tile, layout.tileWidth, layout.tileHeight);
            lv_obj_set_style_pad_bottom(tile, layout.pad, LV_STATE_DEFAULT);
            lv_obj_set_style_pad_row(tile, layout.labelGap, LV_STATE_DEFAULT);
            lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        }
        lv_obj_set_clickable(tile, true);
        lv_obj_set_click_focusable(tile, true);
        lv_obj_set_scrollable(tile, false);
        lv_obj_set_user_data(tile, const_cast<TileGridItem*>(&item));

        lv_obj_t* frame = lvgl_icon_button_create(tile);
        lv_obj_set_size(frame, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(frame, layout.framePad, LV_STATE_DEFAULT);
        // Concentric with the rounded corners of the icon, which have a radius of 22 in 96
        lv_obj_set_style_radius(frame, layout.iconSize * 22 / 96 + layout.framePad, LV_STATE_DEFAULT);
        // Grid navigation only removes direct children from the input group, and the tile is the focus target
        lv_group_remove_obj(frame);
        lv_obj_set_clickable(frame, false);
        lv_obj_set_click_focusable(frame, false);
        lv_obj_set_scrollable(frame, false);
        if (card) {
            // The selection outline comes from the theme, like the one of the icon frame
            lv_obj_set_style_bg_color(tile, lv_color_mix(lv_obj_get_style_text_color(tile, LV_PART_MAIN), lv_obj_get_style_bg_color(tile, LV_PART_MAIN), LV_OPA_20), LV_STATE_PRESSED);
        } else {
            lv_obj_add_event_cb(tile, onTileStateChanged, LV_EVENT_STATE_CHANGED, frame);
        }

        const lv_image_dsc_t* image_src = item.image;
        const char* icon_text = item.icon;
        if (image_src == nullptr && icon_text == nullptr) {
            if (callbacks.resolveImage != nullptr) {
                image_src = callbacks.resolveImage(item, callbacks.userData);
            }
            if (image_src == nullptr && callbacks.resolveIcon != nullptr) {
                icon_text = callbacks.resolveIcon(item, callbacks.userData);
            }
        }
        if (image_src != nullptr) {
            lv_obj_t* image = lv_image_create(frame);
            lv_image_set_src(image, image_src);
            lv_obj_set_size(image, layout.iconSize, layout.iconSize);
            if (item.imageRecolor) {
                lv_color_t color = lv_obj_get_style_text_color(frame, LV_PART_MAIN);
                if (iconColor == IconColor::Primary) {
                    color = lv_theme_get_color_primary(image);
                } else if (iconColor == IconColor::Secondary) {
                    color = lv_theme_get_color_secondary(image);
                }
                lv_obj_set_style_image_recolor(image, color, LV_STATE_DEFAULT);
                lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, LV_STATE_DEFAULT);
            }
        } else {
            lv_obj_t* icon = lv_label_create(frame);
            // The largest shared icon font that fits the icon box, which shrinks with the launcher icon scale
            const bool largeFits = !card && static_cast<int32_t>(lvgl_get_shared_icon_large_font_height()) <= layout.iconSize;
            const int32_t glyphSize = static_cast<int32_t>(largeFits ? lvgl_get_shared_icon_large_font_height() : lvgl_get_shared_icon_default_font_height());
            lv_obj_set_style_text_font(icon, largeFits ? lvgl_get_shared_icon_large_font() : lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
            lv_obj_set_size(icon, layout.iconSize, layout.iconSize);
            if (glyphSize > layout.iconSize) {
                // Not even the smallest font fits, so the glyph is scaled down around its center, which is moved to the center of the box
                lv_obj_set_style_transform_pivot_x(icon, layout.iconSize / 2, LV_STATE_DEFAULT);
                lv_obj_set_style_transform_pivot_y(icon, glyphSize / 2, LV_STATE_DEFAULT);
                lv_obj_set_style_transform_scale(icon, 256 * layout.iconSize / glyphSize, LV_STATE_DEFAULT);
                lv_obj_set_style_translate_y(icon, -(glyphSize - layout.iconSize) / 2, LV_STATE_DEFAULT);
            }
            lv_label_set_text(icon, icon_text != nullptr ? icon_text : "");
            // A placeholder for an image that isn't loaded yet has the theme's text colour
            lv_obj_set_style_text_color(icon, lv_obj_get_style_text_color(frame, LV_PART_MAIN), LV_STATE_DEFAULT);
        }

        lv_obj_t* label = lv_label_create(tile);
        // Favourites have a label in the theme's secondary colour
        lv_obj_set_style_text_color(label, item.highlighted ? lv_theme_get_color_secondary(label) : lv_obj_get_style_text_color(card ? tile : frame, LV_PART_MAIN), LV_STATE_DEFAULT);
        const auto label_font = lvgl_get_text_font(card ? FONT_SIZE_DEFAULT : FONT_SIZE_SMALL);
        lv_obj_set_style_text_font(label, label_font, LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(label, card ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
        // Dots mode needs a fixed height, otherwise the label wraps instead of truncating
        if (card) {
            lv_obj_set_flex_grow(label, 1);
            lv_obj_set_height(label, lv_font_get_line_height(label_font));
        } else {
            lv_obj_set_size(label, LV_PCT(100), lv_font_get_line_height(label_font));
        }
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text(label, item.name.c_str());

        lv_obj_add_event_cb(tile, onTilePressed, LV_EVENT_PRESSED, this);
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
    } else if (selectFirst && group != nullptr) {
        if (lv_obj_get_child_count(grid) > 0) {
            lv_group_focus_obj(grid);
            lv_gridnav_set_focused(grid, lv_obj_get_child(grid, 0), LV_ANIM_OFF);
        } else if (fallback != nullptr) {
            // A widget in a grid navigation container isn't a group member: the container is
            lv_obj_t* container = lv_obj_get_parent(fallback);
            while (container != nullptr && !lvgl_grid_navigation_is_container(container)) {
                container = lv_obj_get_parent(container);
            }
            if (container != nullptr) {
                lv_group_focus_obj(container);
                lv_gridnav_set_focused(container, fallback, LV_ANIM_OFF);
            } else {
                lv_group_focus_obj(fallback);
            }
        }
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
    lv_obj_set_style_pad_row(grid, getTileGap(style == Style::Card), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(grid, getTileGap(style == Style::Card), LV_STATE_DEFAULT);
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

void TileGrid::createWidgets(lv_obj_t* parent) {
    // Widgets of a previous build are deleted by now
    pageIndicator = nullptr;
    createGrid(parent);
    if (systemPageButtons) {
        system_bars_set_page_button_callbacks(onSystemPreviousPage, onSystemNextPage, this);
    }
    pageIndicator = lvgl_page_indicator_create(parent);
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

void TileGrid::destroyWidgets() {
    // The window deletes the widgets, so no pointer may outlive them
    pageIndicator = nullptr;
}

}
