// SPDX-License-Identifier: Apache-2.0
#define LV_USE_PRIVATE_API 1 // For the lv_obj_class_t declaration and lv_obj_t's layout

#include <lvgl/widgets/tile_grid.h>

#include <lvgl/devices/indev.h>
#include <lvgl/fonts.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/theme.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/icon_button.h>
#include <lvgl/widgets/page_indicator.h>

#include <tactility/Memory.hpp>
#include <tactility/log.h>
#include <tactility/std/string.hpp>
#include <tactility/std/vector.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr auto* TAG = "lvgl_tile_grid";

// Minimum label width, as a multiple of the label's font size
constexpr uint32_t MIN_LABEL_WIDTH_IN_FONT_SIZES = 6;
// Reference width of a card, as a multiple of the default font size. Cards grow to fill their row.
constexpr uint32_t CARD_WIDTH_IN_FONT_SIZES = 12;

struct StoredItem {
    tt::std::string id;
    tt::std::string name;
    /** Its id and name point into the strings above */
    LvglTileGridItem item;
};

struct TileGrid {
    lv_obj_t* obj = nullptr;
    /** The grid navigation container of the tiles */
    lv_obj_t* tiles = nullptr;
    lv_obj_t* pageIndicator = nullptr;

    LvglTileGridStyle style = LVGL_TILE_GRID_STYLE_TILE;
    LvglTileGridIconColor iconColor = LVGL_TILE_GRID_ICON_COLOR_DEFAULT;
    bool swipeNavigation = false;
    LvglTileGridCallbacks callbacks = {};
    const LvglTileGridPaging* paging = nullptr;

    /** Tiles point at the items, so they are only replaced after the tiles are deleted */
    tt::std::vector<StoredItem> items;
    bool collecting = false;

    /** Where the pointer was pressed on a tile, which tells a tap from a swipe when it is released */
    lv_point_t pressPoint = {};
    uint32_t page = 0;
    uint32_t pageCount = 1;
    /** The next rebuild shows the first page. The page above is the shown one until then. */
    bool firstPagePending = false;
    /** Whether a rebuild is queued */
    bool repopulatePending = false;
    bool keepSelectionOnRepopulate = true;
    bool selectFirstOnRepopulate = false;
    lv_obj_t* emptyFallback = nullptr;
    /** The size of the tiles container that the current tiles were laid out for */
    int32_t populatedWidth = 0;
    int32_t populatedHeight = 0;
};

struct TileGridObject {
    lv_obj_t obj;
    TileGrid* grid;
};

// Not themed: the grid is an invisible container of the tiles and the page dots
const lv_obj_class_t lvgl_tile_grid_class = {
    .base_class = &lv_obj_class,
    .constructor_cb = nullptr,
    .destructor_cb = nullptr,
    .event_cb = nullptr,
    .user_data = nullptr,
    .name = "tile_grid",
    .width_def = LV_PCT(100),
    .height_def = LV_SIZE_CONTENT,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_FALSE,
    .instance_size = sizeof(TileGridObject),
    .theme_inheritable = LV_OBJ_CLASS_THEME_INHERITABLE_FALSE
};

TileGrid* getGrid(lv_obj_t* obj) {
    if (obj == nullptr || !lv_obj_check_type(obj, &lvgl_tile_grid_class)) {
        return nullptr;
    }
    return reinterpret_cast<TileGridObject*>(obj)->grid;
}

// Scales a spacing in pixels with the device's launcher spacing scale
int32_t scaleSpacing(int32_t spacing) {
    const float scale = lvgl_theme_get_launcher_icon_spacing_scale() / 100.0f;
    return static_cast<int32_t>(std::lround(spacing * scale));
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

PageLayout computePageLayout(lv_obj_t* tiles, LvglTileGridStyle style, int32_t reservedWidth, int32_t reservedHeight) {
    const bool card = style == LVGL_TILE_GRID_STYLE_CARD;
    const auto icon_size = static_cast<int32_t>(card ? lvgl_tile_grid_get_card_icon_size() : lvgl_tile_grid_get_tile_icon_size());
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
    // The tiles are kept the gap away from the edges. The padding is set from the offsets, so it isn't part of the measurement.
    // The width that the tiles can use, without the space reserved for the page dots
    const auto grid_width = lv_obj_get_width(tiles) - reservedWidth;
    const auto available_width = grid_width - 2 * gap;
    const auto grid_height = lv_obj_get_height(tiles) - reservedHeight;
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

const LvglTileGridItem* getTileItem(lv_event_t* e) {
    return static_cast<const LvglTileGridItem*>(lv_obj_get_user_data(lv_event_get_target_obj(e)));
}

// The page indicator is a row centered below the tiles, or a column at their side
void placePageIndicator(lv_obj_t* indicator, lv_obj_t* tiles, bool column, bool alignRight) {
    if (column) {
        lv_obj_set_floating(indicator, true);
        lvgl_page_indicator_set_vertical(indicator, true);
        lv_obj_set_width(indicator, LV_SIZE_CONTENT);
        lv_obj_remove_local_style_prop(indicator, LV_STYLE_MARGIN_BOTTOM, LV_PART_MAIN);
        lv_obj_update_layout(indicator);
        // Centered on the tiles, which can have something above them, like a toolbar.
        // The alignment is within the padding of the tiles container, which has the tiles in it.
        const int32_t padLeft = lv_obj_get_style_pad_left(tiles, LV_PART_MAIN);
        const int32_t padTop = lv_obj_get_style_pad_top(tiles, LV_PART_MAIN);
        lv_obj_align_to(
            indicator, tiles, alignRight ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID,
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

void populate(TileGrid* self);

void goToPage(TileGrid* self, bool next) {
    if (self->firstPagePending) {
        self->firstPagePending = false;
        self->page = 0;
    }
    if (next) {
        self->page = self->page + 1 < self->pageCount ? self->page + 1 : 0;
    } else {
        self->page = self->page > 0 ? self->page - 1 : self->pageCount - 1;
    }
    // Changing the page doesn't keep the selected item selected: that would return to the selected item's page
    self->keepSelectionOnRepopulate = false;
    populate(self);
    if (self->callbacks.on_page_changed != nullptr) {
        self->callbacks.on_page_changed(self->obj, self->callbacks.user_data);
    }
}

void onDeferredNextPage(void* userData) {
    goToPage(static_cast<TileGrid*>(userData), true);
}

void onDeferredPreviousPage(void* userData) {
    goToPage(static_cast<TileGrid*>(userData), false);
}

void onDeferredRepopulate(void* userData) {
    auto* self = static_cast<TileGrid*>(userData);
    self->repopulatePending = false;
    populate(self);
}

// A layout pass can resize the grid temporarily (e.g. to its content height, before the parent's flex layout grows it again)
void onDeferredResize(void* userData) {
    auto* self = static_cast<TileGrid*>(userData);
    if (lv_obj_get_width(self->tiles) != self->populatedWidth || lv_obj_get_height(self->tiles) != self->populatedHeight) {
        populate(self);
    }
}

void onDeleted(lv_event_t* e) {
    auto* self = static_cast<TileGrid*>(lv_event_get_user_data(e));
    // Cancels the deferred work that was scheduled right before the grid was deleted (e.g. the app closing)
    lv_async_call_cancel(onDeferredRepopulate, self);
    lv_async_call_cancel(onDeferredResize, self);
    lv_async_call_cancel(onDeferredNextPage, self);
    lv_async_call_cancel(onDeferredPreviousPage, self);
    reinterpret_cast<TileGridObject*>(self->obj)->grid = nullptr;
    tt::deleteObject(self);
}

// Deferred: page size depends on the size of the tiles container, which is final only after the layout pass that triggered this
void onTilesSizeChanged(lv_event_t* e) {
    // One pending check is enough, also when the size changes several times before it runs
    lv_async_call_cancel(onDeferredResize, lv_event_get_user_data(e));
    lv_async_call(onDeferredResize, lv_event_get_user_data(e));
}

// Swipes that start on a tile reach the tiles container too, as tiles pass their gestures on to it
void onGesture(lv_event_t* e) {
    auto* self = static_cast<TileGrid*>(lv_event_get_user_data(e));
    lv_indev_t* indev = lv_indev_active();
    if (indev == nullptr || self->pageCount <= 1) {
        return;
    }
    const lv_dir_t direction = lv_indev_get_gesture_dir(indev);
    const bool vertical = self->paging != nullptr && self->paging->is_vertical_swipe != nullptr && self->paging->is_vertical_swipe(self->obj);
    const lv_dir_t next = vertical ? LV_DIR_TOP : LV_DIR_LEFT;
    const lv_dir_t previous = vertical ? LV_DIR_BOTTOM : LV_DIR_RIGHT;
    if (direction != next && direction != previous) {
        return;
    }
    if (self->paging != nullptr && self->paging->ignore_swipe != nullptr && self->paging->ignore_swipe(self->obj)) {
        return;
    }
    // Keeps the tile where the swipe started from being clicked when the finger is lifted
    lv_indev_wait_release(indev);
    // Deferred: changing the page deletes the tiles, possibly including the one this event came from
    lv_async_call(direction == next ? onDeferredNextPage : onDeferredPreviousPage, self);
}

void onTileStateChanged(lv_event_t* e) {
    constexpr auto MIRRORED = static_cast<lv_state_t>(LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY | LV_STATE_PRESSED | LV_STATE_CHECKED);
    lv_obj_t* tile = lv_event_get_target_obj(e);
    auto* frame = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    const lv_state_t state = lv_obj_get_state(tile);
    lv_obj_remove_state(frame, static_cast<lv_state_t>(MIRRORED & ~state));
    lv_obj_add_state(frame, static_cast<lv_state_t>(MIRRORED & state));
}

void onTilePressed(lv_event_t* e) {
    auto* self = static_cast<TileGrid*>(lv_event_get_user_data(e));
    lv_indev_t* indev = lv_indev_active();
    if (indev != nullptr && lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
        lv_indev_get_point(indev, &self->pressPoint);
    }
}

void onTileClicked(lv_event_t* e) {
    auto* self = static_cast<TileGrid*>(lv_event_get_user_data(e));
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
    if (self->callbacks.on_clicked != nullptr) {
        self->callbacks.on_clicked(self->obj, getTileItem(e), self->callbacks.user_data);
    }
}

void onTileLongPressed(lv_event_t* e) {
    auto* self = static_cast<TileGrid*>(lv_event_get_user_data(e));
    self->callbacks.on_long_pressed(self->obj, getTileItem(e), self->callbacks.user_data);
}

void onTileKey(lv_event_t* e) {
    auto* self = static_cast<TileGrid*>(lv_event_get_user_data(e));
    self->callbacks.on_key(self->obj, getTileItem(e), lv_event_get_key(e), self->callbacks.user_data);
}

bool isPagingVisible(const TileGrid* self) {
    return !lv_obj_is_hidden(self->pageIndicator);
}

void setPagingVisible(TileGrid* self, bool visible) {
    lv_obj_set_hidden(self->pageIndicator, !visible);
    if (self->paging != nullptr && self->paging->on_paging_visible != nullptr) {
        self->paging->on_paging_visible(self->obj, visible);
    }
}

void collectItems(TileGrid* self) {
    self->items.clear();
    self->collecting = true;
    if (self->callbacks.collect != nullptr) {
        self->callbacks.collect(self->obj, self->callbacks.user_data);
    }
    self->collecting = false;
    // The strings have their final addresses now
    for (auto& stored : self->items) {
        stored.item.id = stored.id.c_str();
        stored.item.name = stored.name.c_str();
    }
}

lv_obj_t* createTile(TileGrid* self, const LvglTileGridItem& item, const PageLayout& layout) {
    // The tile receives the input. A tile's icon frame shows the focus, pressed and checked states of the theme's icon button,
    // and a card shows them itself.
    const bool card = self->style == LVGL_TILE_GRID_STYLE_CARD;
    lv_obj_t* tile;
    if (card) {
        tile = lvgl_card_create(self->tiles);
        lv_obj_set_size(tile, layout.tileWidth, layout.tileHeight);
        lv_obj_set_style_pad_all(tile, layout.pad, LV_STATE_DEFAULT);
        lv_obj_set_style_pad_column(tile, layout.pad, LV_STATE_DEFAULT);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    } else {
        tile = lv_obj_create(self->tiles);
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
    lv_obj_set_user_data(tile, const_cast<LvglTileGridItem*>(&item));
    if (item.checked) {
        // A card shows it itself, and the icon frame of a tile gets it below
        lv_obj_add_state(tile, LV_STATE_CHECKED);
    }

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
        if (item.checked) {
            lv_obj_add_state(frame, LV_STATE_CHECKED);
        }
    }

    const lv_image_dsc_t* image_src = item.image;
    const char* icon_text = item.icon;
    if (image_src == nullptr && icon_text == nullptr) {
        if (self->callbacks.resolve_image != nullptr) {
            image_src = self->callbacks.resolve_image(self->obj, &item, self->callbacks.user_data);
        }
        if (image_src == nullptr && self->callbacks.resolve_icon != nullptr) {
            icon_text = self->callbacks.resolve_icon(self->obj, &item, self->callbacks.user_data);
        }
    }
    auto getIconColor = [self, frame](lv_obj_t* object) {
        if (self->iconColor == LVGL_TILE_GRID_ICON_COLOR_PRIMARY) {
            return lv_theme_get_color_primary(object);
        }
        if (self->iconColor == LVGL_TILE_GRID_ICON_COLOR_SECONDARY) {
            return lv_theme_get_color_secondary(object);
        }
        return lv_obj_get_style_text_color(frame, LV_PART_MAIN);
    };
    if (image_src != nullptr) {
        lv_obj_t* image = lv_image_create(frame);
        lv_image_set_src(image, image_src);
        lv_obj_set_size(image, layout.iconSize, layout.iconSize);
        if (item.image_recolor) {
            lv_obj_set_style_image_recolor(image, getIconColor(image), LV_STATE_DEFAULT);
            lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, LV_STATE_DEFAULT);
        }
    } else {
        lv_obj_t* icon = lv_label_create(frame);
        // The largest shared icon font that fits the icon box, which shrinks with the launcher icon scale
        const bool largeFits = !card && static_cast<int32_t>(lvgl_get_shared_icon_large_font_height()) <= layout.iconSize;
        const int32_t glyphSize = static_cast<int32_t>(largeFits ? lvgl_get_shared_icon_large_font_height() : lvgl_get_shared_icon_default_font_height());
        lv_obj_set_style_text_font(icon, largeFits ? lvgl_get_shared_icon_large_font() : lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
        // The frame centers the glyph in both directions. The glyph's own line height decides its size, not the nominal font height.
        const int32_t frameSize = layout.iconSize + 2 * layout.framePad;
        lv_obj_set_size(frame, frameSize, frameSize);
        lv_obj_set_flex_flow(frame, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(frame, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_size(icon, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        if (glyphSize > layout.iconSize) {
            // Not even the smallest font fits, so the glyph is scaled down around its center
            lv_obj_set_style_transform_pivot_x(icon, LV_PCT(50), LV_STATE_DEFAULT);
            lv_obj_set_style_transform_pivot_y(icon, LV_PCT(50), LV_STATE_DEFAULT);
            lv_obj_set_style_transform_scale(icon, 256 * layout.iconSize / glyphSize, LV_STATE_DEFAULT);
        }
        lv_label_set_text(icon, icon_text != nullptr ? icon_text : "");
        lv_obj_set_style_text_color(icon, getIconColor(icon), LV_STATE_DEFAULT);
    }

    lv_obj_t* label = lv_label_create(tile);
    // Highlighted items have a label in the theme's secondary colour
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
    lv_label_set_text(label, item.name);

    lv_obj_add_event_cb(tile, onTilePressed, LV_EVENT_PRESSED, self);
    lv_obj_add_event_cb(tile, onTileClicked, LV_EVENT_SHORT_CLICKED, self);
    if (self->callbacks.on_long_pressed != nullptr) {
        lv_obj_add_event_cb(tile, onTileLongPressed, LV_EVENT_LONG_PRESSED, self);
    }
    if (self->callbacks.on_key != nullptr) {
        lv_obj_add_event_cb(tile, onTileKey, LV_EVENT_KEY, self);
    }
    return tile;
}

void populate(TileGrid* self) {
    lv_obj_t* tiles = self->tiles;
    // Captured before lv_obj_clean() deletes the currently focused tile below: creating
    // replacement tiles doesn't restore the focus, so the item id is remembered here and
    // re-focused on its new tile once rebuilt.
    tt::std::string focusedId;
    const bool keepSelection = self->keepSelectionOnRepopulate;
    lv_group_t* group = lv_obj_get_group(tiles);
    if (group != nullptr && keepSelection) {
        // Grid navigation focuses the tiles container in the group, and a tile within it
        lv_obj_t* focused = lv_group_get_focused(group) == tiles ? lvgl_grid_navigation_get_focused(tiles) : nullptr;
        if (focused != nullptr && lv_obj_get_parent(focused) == tiles) {
            focusedId = static_cast<const LvglTileGridItem*>(lv_obj_get_user_data(focused))->id;
        }
    }
    self->keepSelectionOnRepopulate = true;
    const bool selectFirst = self->selectFirstOnRepopulate;
    lv_obj_t* fallback = self->emptyFallback;
    self->selectFirstOnRepopulate = false;
    self->emptyFallback = nullptr;
    if (self->firstPagePending) {
        self->firstPagePending = false;
        self->page = 0;
    }

    // Old tiles hold pointers into the items, so they are deleted before the items are replaced.
    lv_obj_clean(tiles);
    if (self->callbacks.on_tiles_cleared != nullptr) {
        self->callbacks.on_tiles_cleared(self->obj, self->callbacks.user_data);
    }
    collectItems(self);

    // The spacing is computed for a full page, so a partially filled last page keeps its rows in the same positions.
    lv_obj_set_style_pad_top(tiles, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(tiles, 0, LV_STATE_DEFAULT);
    const LvglTileGridIndicator indicator = self->paging != nullptr && self->paging->get_indicator != nullptr
        ? self->paging->get_indicator(self->obj)
        : LVGL_TILE_GRID_INDICATOR_BELOW;
    const bool dotsInColumn = indicator != LVGL_TILE_GRID_INDICATOR_BELOW;
    const bool dotsRight = indicator == LVGL_TILE_GRID_INDICATOR_RIGHT;
    // The same space is left free on the other side, so the tiles stay centered
    const int32_t columnWidth = dotsInColumn ? getPageIndicatorColumnWidth() : 0;
    const int32_t reservedWidth = 2 * columnWidth;
    // A row of dots below the tiles is balanced by the same space above them
    auto getReservedHeight = [&] { return !dotsInColumn && isPagingVisible(self) ? getPageIndicatorRowHeight() : 0; };
    // Measured without the indicator first, so the result doesn't depend on the previous rebuild
    const bool wasPagingVisible = isPagingVisible(self);
    if (wasPagingVisible) {
        lv_obj_set_hidden(self->pageIndicator, true);
        lv_obj_update_layout(lv_obj_get_parent(self->obj));
    }
    PageLayout layout = computePageLayout(tiles, self->style, reservedWidth, getReservedHeight());
    const uint32_t item_count = self->items.size();
    uint32_t page_count = std::max<uint32_t>(1, (item_count + layout.pageSize - 1) / layout.pageSize);

    // The page indicator and buttons take room from the tiles, and are only shown with multiple pages.
    // Showing them changes the size and so the page size, so the layout is computed again with them in place.
    const bool paged = page_count > 1;
    setPagingVisible(self, paged);
    if (paged || wasPagingVisible) {
        lv_obj_update_layout(lv_obj_get_parent(self->obj));
        layout = computePageLayout(tiles, self->style, reservedWidth, getReservedHeight());
        page_count = std::max<uint32_t>(1, (item_count + layout.pageSize - 1) / layout.pageSize);
        setPagingVisible(self, page_count > 1);
    }
    self->populatedWidth = lv_obj_get_width(tiles);
    self->populatedHeight = lv_obj_get_height(tiles);
    lv_obj_set_style_pad_row(tiles, layout.verticalSpacing, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(tiles, layout.horizontalSpacing, LV_STATE_DEFAULT);

    // Follows the focused item onto its new page, e.g. after a reorder moved it.
    if (!focusedId.empty()) {
        const auto it = std::ranges::find_if(self->items, [&](const StoredItem& stored) {
            return focusedId == stored.id;
        });
        if (it != self->items.end()) {
            self->page = static_cast<uint32_t>(it - self->items.begin()) / layout.pageSize;
        }
    }
    self->page = std::min(self->page, page_count - 1);
    self->pageCount = page_count;

    // The rows of a page of cards that isn't full are centered vertically
    int32_t top = layout.topMargin;
    if (self->style == LVGL_TILE_GRID_STYLE_CARD) {
        const auto count = static_cast<int32_t>(std::min<uint32_t>(item_count - self->page * layout.pageSize, layout.pageSize));
        const auto used_rows = (count + layout.columns - 1) / layout.columns;
        top += (layout.rows - used_rows) * (layout.tileHeight + layout.verticalSpacing) / 2;
    }
    lv_obj_set_style_pad_top(tiles, top + getReservedHeight(), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(tiles, layout.leftMargin + columnWidth, LV_STATE_DEFAULT);

    lvgl_page_indicator_set_page_count(self->pageIndicator, page_count);
    lvgl_page_indicator_set_page(self->pageIndicator, self->page);
    placePageIndicator(self->pageIndicator, tiles, dotsInColumn, dotsRight);

    lv_obj_t* focusedTile = nullptr;
    const uint32_t first = self->page * layout.pageSize;
    const uint32_t last = std::min(item_count, first + layout.pageSize);
    for (uint32_t i = first; i < last; i++) {
        const auto& stored = self->items[i];
        lv_obj_t* tile = createTile(self, stored.item, layout);
        if (!focusedId.empty() && focusedId == stored.id) {
            focusedTile = tile;
        }
    }

    if (focusedTile != nullptr) {
        lv_group_focus_obj(tiles);
        lv_gridnav_set_focused(tiles, focusedTile, LV_ANIM_OFF);
    } else if (selectFirst && group != nullptr) {
        if (lv_obj_get_child_count(tiles) > 0) {
            lv_group_focus_obj(tiles);
            lv_gridnav_set_focused(tiles, lv_obj_get_child(tiles, 0), LV_ANIM_OFF);
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
    } else if (!keepSelection && group != nullptr && lv_group_get_focused(group) == tiles) {
        // Grid navigation selects the first new tile when the tiles are focused, but a page change shows no selection.
        // The next key that moves the focus selects a tile again.
        lvgl_focus_hide_key_selection(group);
    }
}

} // namespace

extern "C" {

uint32_t lvgl_tile_grid_get_tile_icon_size(void) {
    const float scale = lvgl_theme_get_launcher_icon_size_scale() / 100.0f;
    return static_cast<uint32_t>(std::lround(lvgl_get_text_font_height(FONT_SIZE_DEFAULT) * 2.5f * scale));
}

uint32_t lvgl_tile_grid_get_card_icon_size(void) {
    return static_cast<uint32_t>(lvgl_get_text_font_height(FONT_SIZE_DEFAULT));
}

lv_obj_t* lvgl_tile_grid_create(lv_obj_t* parent, const LvglTileGridConfig* config) {
    auto* self = tt::newObject<TileGrid>();
    if (self == nullptr) {
        return nullptr;
    }
    if (config != nullptr) {
        self->style = config->style;
        self->iconColor = config->icon_color;
        self->swipeNavigation = config->swipe_navigation;
        self->callbacks = config->callbacks;
        self->paging = config->paging;
        self->page = config->page;
    }

    lv_obj_t* obj = lv_obj_class_create_obj(&lvgl_tile_grid_class, parent);
    lv_obj_class_init_obj(obj);
    reinterpret_cast<TileGridObject*>(obj)->grid = self;
    self->obj = obj;
    lv_obj_set_flex_grow(obj, 1);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(obj, false);
    lv_obj_add_event_cb(obj, onDeleted, LV_EVENT_DELETE, self);

    lv_obj_t* tiles = lv_obj_create(obj);
    self->tiles = tiles;
    lv_obj_set_width(tiles, LV_PCT(100));
    lv_obj_set_flex_grow(tiles, 1);
    lv_obj_set_flex_flow(tiles, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_all(tiles, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(tiles, getTileGap(self->style == LVGL_TILE_GRID_STYLE_CARD), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(tiles, getTileGap(self->style == LVGL_TILE_GRID_STYLE_CARD), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(tiles, 0, LV_STATE_DEFAULT);
    lv_obj_set_scrollable(tiles, false);
    // The arrow keys move between the tiles in rows and columns
    lvgl_grid_navigation_add(tiles);
    if (self->swipeNavigation) {
        // LVGL sends a gesture to the first object, starting at the touched one, that doesn't pass it on to its parent
        lv_obj_set_gesture_bubble(tiles, false);
        lv_obj_add_event_cb(tiles, onGesture, LV_EVENT_GESTURE, self);
    }

    self->pageIndicator = lvgl_page_indicator_create(obj);

    // Resolves the flex grow height, which the page size is derived from
    lv_obj_update_layout(parent);
    lv_obj_add_event_cb(tiles, onTilesSizeChanged, LV_EVENT_SIZE_CHANGED, self);

    populate(self);

    if (!lvgl_indev_exists(LV_INDEV_TYPE_POINTER) && lv_obj_get_child_count(tiles) > 0) {
        lv_group_focus_obj(tiles);
        lv_gridnav_set_focused(tiles, lv_obj_get_child(tiles, 0), LV_ANIM_OFF);
    }
    return obj;
}

void lvgl_tile_grid_add_item(lv_obj_t* grid, const LvglTileGridItem* item) {
    TileGrid* self = getGrid(grid);
    if (self == nullptr || item == nullptr || !self->collecting) {
        return;
    }
    StoredItem stored { item->id != nullptr ? item->id : "", item->name != nullptr ? item->name : "", *item };
    self->items.push_back(std::move(stored));
}

void lvgl_tile_grid_set_style(lv_obj_t* grid, LvglTileGridStyle style) {
    if (TileGrid* self = getGrid(grid)) {
        self->style = style;
    }
}

void lvgl_tile_grid_set_icon_color(lv_obj_t* grid, LvglTileGridIconColor color) {
    if (TileGrid* self = getGrid(grid)) {
        self->iconColor = color;
    }
}

void lvgl_tile_grid_show_first_page(lv_obj_t* grid) {
    if (TileGrid* self = getGrid(grid)) {
        self->firstPagePending = true;
    }
}

void lvgl_tile_grid_request_repopulate(lv_obj_t* grid, bool keep_selection) {
    TileGrid* self = getGrid(grid);
    if (self == nullptr) {
        return;
    }
    // The latest request decides the selection policy
    self->keepSelectionOnRepopulate = keep_selection;
    self->selectFirstOnRepopulate = false;
    self->emptyFallback = nullptr;
    // A queued rebuild applies the latest request, so it is kept instead of being replaced
    if (!self->repopulatePending) {
        self->repopulatePending = lv_async_call(onDeferredRepopulate, self) == LV_RESULT_OK;
        if (!self->repopulatePending) {
            LOG_E(TAG, "Failed to queue a rebuild");
        }
    }
}

void lvgl_tile_grid_request_repopulate_selecting_first(lv_obj_t* grid, lv_obj_t* empty_fallback) {
    TileGrid* self = getGrid(grid);
    if (self == nullptr) {
        return;
    }
    lvgl_tile_grid_request_repopulate(grid, false);
    self->selectFirstOnRepopulate = true;
    self->emptyFallback = empty_fallback;
}

void lvgl_tile_grid_replace_empty_fallback(lv_obj_t* grid, lv_obj_t* empty_fallback) {
    TileGrid* self = getGrid(grid);
    if (self != nullptr && self->selectFirstOnRepopulate) {
        self->emptyFallback = empty_fallback;
    }
}

void lvgl_tile_grid_next_page(lv_obj_t* grid) {
    if (TileGrid* self = getGrid(grid)) {
        goToPage(self, true);
    }
}

void lvgl_tile_grid_previous_page(lv_obj_t* grid) {
    if (TileGrid* self = getGrid(grid)) {
        goToPage(self, false);
    }
}

uint32_t lvgl_tile_grid_get_page(lv_obj_t* grid) {
    TileGrid* self = getGrid(grid);
    return self != nullptr ? self->page : 0;
}

lv_obj_t* lvgl_tile_grid_find_tile(lv_obj_t* grid, const char* id) {
    TileGrid* self = getGrid(grid);
    if (self == nullptr || id == nullptr) {
        return nullptr;
    }
    const uint32_t count = lv_obj_get_child_count(self->tiles);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t* tile = lv_obj_get_child(self->tiles, static_cast<int32_t>(i));
        const auto* item = static_cast<const LvglTileGridItem*>(lv_obj_get_user_data(tile));
        if (item != nullptr && strcmp(item->id, id) == 0) {
            return tile;
        }
    }
    return nullptr;
}

const LvglTileGridItem* lvgl_tile_grid_get_tile_item(lv_obj_t* grid, lv_obj_t* tile) {
    TileGrid* self = getGrid(grid);
    if (self == nullptr || tile == nullptr || lv_obj_get_parent(tile) != self->tiles) {
        return nullptr;
    }
    return static_cast<const LvglTileGridItem*>(lv_obj_get_user_data(tile));
}

lv_obj_t* lvgl_tile_grid_get_tiles(lv_obj_t* grid) {
    TileGrid* self = getGrid(grid);
    return self != nullptr ? self->tiles : nullptr;
}

} // extern "C"
