#include <Tactility/app/AppGrid.h>

#include <lvgl/devices/indev.h>
#include <lvgl/fonts.h>
#include <lvgl/widgets/icon_button.h>
#include <lvgl/widgets/toolbar.h>

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

const AppGridItem* getTileItem(lv_event_t* e) {
    return static_cast<const AppGridItem*>(lv_obj_get_user_data(lv_event_get_target_obj(e)));
}

} // namespace

void AppGrid::onDeferredRepopulate(void* userData) {
    static_cast<AppGrid*>(userData)->populate();
}

void AppGrid::onGridDeleted(lv_event_t* e) {
    // Cancels a still-pending repopulate scheduled right before the grid itself was deleted (e.g. the app closing)
    lv_async_call_cancel(onDeferredRepopulate, lv_event_get_user_data(e));
}

// Deferred: page size depends on the grid size, which is final only after the layout pass that triggered this.
void AppGrid::onGridSizeChanged(lv_event_t* e) {
    lv_async_call(onDeferredRepopulate, lv_event_get_user_data(e));
}

void AppGrid::onPrevPressed(lv_event_t* e) {
    auto* self = static_cast<AppGrid*>(lv_event_get_user_data(e));
    if (self->page > 0) {
        self->page--;
        self->populate();
    }
}

void AppGrid::onNextPressed(lv_event_t* e) {
    auto* self = static_cast<AppGrid*>(lv_event_get_user_data(e));
    self->page++;
    self->populate();
}

void AppGrid::onTileClicked(lv_event_t* e) {
    const auto* self = static_cast<AppGrid*>(lv_event_get_user_data(e));
    self->callbacks.onClicked(getTileItem(e)->manifest, self->callbacks.userData);
}

void AppGrid::onTileLongPressed(lv_event_t* e) {
    const auto* self = static_cast<AppGrid*>(lv_event_get_user_data(e));
    self->callbacks.onLongPressed(getTileItem(e)->manifest, self->callbacks.userData);
}

void AppGrid::onTileKey(lv_event_t* e) {
    const auto* self = static_cast<AppGrid*>(lv_event_get_user_data(e));
    self->callbacks.onKey(getTileItem(e)->manifest, lv_event_get_key(e), self->callbacks.userData);
}

void AppGrid::requestRepopulate(bool keepSelection) {
    keepSelectionOnRepopulate = keepSelection;
    lv_async_call(onDeferredRepopulate, this);
}

void AppGrid::populate() {
    // Captured before lv_obj_clean() deletes the currently focused tile below: LVGL moves
    // focus elsewhere as it leaves the group, and creating replacement tiles doesn't restore
    // it, so the app id is remembered here and re-focused on its new tile once rebuilt.
    std::string focusedAppId;
    lv_group_t* group = lv_group_get_default();
    if (group != nullptr && keepSelectionOnRepopulate) {
        lv_obj_t* focused = lv_group_get_focused(group);
        if (focused != nullptr && lv_obj_get_parent(focused) == grid) {
            focusedAppId = static_cast<const AppGridItem*>(lv_obj_get_user_data(focused))->manifest.id;
        }
    }
    keepSelectionOnRepopulate = true;

    // Old tiles hold pointers into items, so they are deleted before items is replaced.
    lv_obj_clean(grid);
    items = callbacks.collect(callbacks.userData);

    // Centers a full page of rows, so a partially filled last page keeps its rows in the same positions.
    lv_obj_set_style_pad_top(grid, 0, LV_STATE_DEFAULT);
    const PageLayout layout = computePageLayout(grid);
    lv_obj_set_style_pad_top(grid, layout.topOffset, LV_STATE_DEFAULT);

    const uint32_t item_count = items.size();
    const uint32_t page_count = std::max<uint32_t>(1, (item_count + layout.pageSize - 1) / layout.pageSize);
    // Follows the focused app onto its new page, e.g. after a reorder moved it.
    if (!focusedAppId.empty()) {
        const auto it = std::ranges::find_if(items, [&](const AppGridItem& item) {
            return focusedAppId == item.manifest.id;
        });
        if (it != items.end()) {
            page = static_cast<uint32_t>(it - items.begin()) / layout.pageSize;
        }
    }
    page = std::min(page, page_count - 1);

    lv_obj_set_state(prevButton, LV_STATE_DISABLED, page == 0);
    lv_obj_set_state(nextButton, LV_STATE_DISABLED, page + 1 >= page_count);
    lv_obj_set_flag(prevButton, LV_OBJ_FLAG_HIDDEN, page_count <= 1);
    lv_obj_set_flag(nextButton, LV_OBJ_FLAG_HIDDEN, page_count <= 1);

    lv_obj_t* focusedTile = nullptr;
    const uint32_t first = page * layout.pageSize;
    const uint32_t last = std::min(item_count, first + layout.pageSize);
    for (uint32_t i = first; i < last; i++) {
        const auto& item = items[i];

        // The theme styles the tile as an icon button: its focus, pressed and selected (favourite) states
        lv_obj_t* tile = lvgl_icon_button_create(grid);
        if (item.highlighted) {
            lv_obj_add_state(tile, LV_STATE_CHECKED);
        }
        lv_obj_set_size(tile, layout.tileWidth, layout.tileHeight);
        lv_obj_set_style_pad_all(tile, layout.pad, LV_STATE_DEFAULT);
        lv_obj_set_style_pad_row(tile, layout.pad / 2, LV_STATE_DEFAULT);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_user_data(tile, const_cast<AppGridItem*>(&item));

        lv_obj_t* icon = lv_label_create(tile);
        lv_obj_set_style_text_font(icon, lvgl_get_shared_icon_large_font(), LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
        lv_obj_set_size(icon, layout.iconSize, layout.iconSize);
        lv_label_set_text(icon, item.icon);

        lv_obj_t* label = lv_label_create(tile);
        lv_obj_set_style_text_font(label, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
        // Dots mode needs a fixed height, otherwise the label wraps instead of truncating
        lv_obj_set_size(label, LV_PCT(100), lv_font_get_line_height(lvgl_get_text_font(FONT_SIZE_SMALL)));
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text(label, item.manifest.name);

        lv_obj_add_event_cb(tile, onTileClicked, LV_EVENT_SHORT_CLICKED, this);
        if (callbacks.onLongPressed != nullptr) {
            lv_obj_add_event_cb(tile, onTileLongPressed, LV_EVENT_LONG_PRESSED, this);
        }
        if (callbacks.onKey != nullptr) {
            lv_obj_add_event_cb(tile, onTileKey, LV_EVENT_KEY, this);
        }

        if (!focusedAppId.empty() && focusedAppId == item.manifest.id) {
            focusedTile = tile;
        }
    }

    if (focusedTile != nullptr) {
        lv_group_focus_obj(focusedTile);
        lv_obj_add_state(focusedTile, LV_STATE_FOCUS_KEY);
    }
}

void AppGrid::createWidgets(lv_obj_t* parent, lv_obj_t* toolbar) {
    prevButton = lvgl_toolbar_add_text_button_action(toolbar, "<", onPrevPressed, this);
    nextButton = lvgl_toolbar_add_text_button_action(toolbar, ">", onNextPressed, this);

    grid = lv_obj_create(parent);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_all(grid, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(grid, TILE_GAP, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(grid, TILE_GAP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(grid, 0, LV_STATE_DEFAULT);
    lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    // Resolves the grid's flex_grow height, which the page size is derived from.
    lv_obj_update_layout(parent);

    lv_obj_add_event_cb(grid, onGridDeleted, LV_EVENT_DELETE, this);
    lv_obj_add_event_cb(grid, onGridSizeChanged, LV_EVENT_SIZE_CHANGED, this);

    populate();

    const bool next_visible = !lv_obj_has_flag(nextButton, LV_OBJ_FLAG_HIDDEN);
    if (next_visible && !lvgl_indev_exists(LV_INDEV_TYPE_POINTER)) {
        lv_group_focus_obj(nextButton);
        lv_obj_add_state(nextButton, LV_STATE_FOCUS_KEY);
    }
}

}
