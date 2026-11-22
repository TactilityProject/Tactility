// SPDX-License-Identifier: Apache-2.0
#define LV_USE_PRIVATE_API 1 // For the lv_obj_class_t and lv_obj_t declarations

#include <lvgl/widgets/tabs.h>

#include <lvgl/fonts.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/widgets/badge.h>

#include <algorithm>
#include <vector>

namespace {

struct Tabs {
    lv_obj_t obj;
    LvglTabsMode mode;
    /** The tabs use the small text font, as they don't fit with the regular one */
    bool small_font;
    /** The tabs only show their icon, as they don't fit with the small font either */
    bool icon_only;
};

struct Tab {
    lv_button_t button;
    lv_obj_t* label;
    /** In front of the label, or null */
    lv_obj_t* icon;
    /** Null while it isn't shown */
    lv_obj_t* badge;
    /** Measured when the text is set: an ellipsis changes the text of the label */
    int32_t text_width;
    int32_t text_width_small;
    int32_t icon_width;
};

void tabs_constructor(const lv_obj_class_t*, lv_obj_t* obj) {
    reinterpret_cast<Tabs*>(obj)->mode = LVGL_TABS_MODE_FIXED;
    reinterpret_cast<Tabs*>(obj)->small_font = false;
    reinterpret_cast<Tabs*>(obj)->icon_only = false;
}

void tab_constructor(const lv_obj_class_t*, lv_obj_t* obj) {
    auto* tab = reinterpret_cast<Tab*>(obj);
    tab->label = nullptr;
    tab->icon = nullptr;
    tab->badge = nullptr;
    tab->text_width = 0;
    tab->text_width_small = 0;
    tab->icon_width = 0;
}

}

// Not static: the theme styles the tab bar and its tabs by these classes
extern "C" const lv_obj_class_t lvgl_tabs_class = {
    .base_class = &lv_obj_class,
    .constructor_cb = tabs_constructor,
    .destructor_cb = nullptr,
    .event_cb = nullptr,
    .user_data = nullptr,
    .name = "tabs",
    .width_def = 0,
    .height_def = 0,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_INHERIT,
    .instance_size = sizeof(Tabs),
    .theme_inheritable = LV_OBJ_CLASS_THEME_INHERITABLE_FALSE
};

extern "C" const lv_obj_class_t lvgl_tabs_tab_class = {
    .base_class = &lv_button_class,
    .constructor_cb = tab_constructor,
    .destructor_cb = nullptr,
    .event_cb = nullptr,
    .user_data = nullptr,
    .name = "tabs_tab",
    .width_def = 0,
    .height_def = 0,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_INHERIT,
    .instance_size = sizeof(Tab),
    .theme_inheritable = LV_OBJ_CLASS_THEME_INHERITABLE_FALSE
};

namespace {

LvglTabsMode get_mode(lv_obj_t* tabs) {
    return reinterpret_cast<Tabs*>(tabs)->mode;
}

Tab* get_tab_data(lv_obj_t* tab) {
    return reinterpret_cast<Tab*>(tab);
}

lv_obj_t* get_label(lv_obj_t* tab) {
    return get_tab_data(tab)->label;
}

bool is_small_font(lv_obj_t* tab) {
    return reinterpret_cast<Tabs*>(lv_obj_get_parent(tab))->small_font;
}

bool is_icon_only(lv_obj_t* tab) {
    return reinterpret_cast<Tabs*>(lv_obj_get_parent(tab))->icon_only;
}

/**
 * Fixed and wrapped tabs end their label in an ellipsis when it doesn't fit, and hide it when the tabs only show their icon.
 * The label needs a fixed height, as it would wrap otherwise.
 */
void fit_label(lv_obj_t* tab) {
    auto* data = get_tab_data(tab);
    auto* label = data->label;
    if (reinterpret_cast<Tabs*>(lv_obj_get_parent(tab))->mode == LVGL_TABS_MODE_SCROLLABLE) {
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_size(label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_hidden(label, false);
        return;
    }
    lv_obj_set_hidden(label, is_icon_only(tab));
    const int32_t text_width = is_small_font(tab) ? data->text_width_small : data->text_width;
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(label, std::clamp<int32_t>(lv_obj_get_content_width(tab), 0, text_width), lv_font_get_line_height(lv_obj_get_style_text_font(label, LV_PART_MAIN)));
}

void on_tab_size_changed(lv_event_t* event) {
    fit_label(lv_event_get_current_target_obj(event));
}

int32_t measure_width(const char* text, const lv_font_t* font, int32_t letter_space) {
    lv_point_t size;
    lv_text_get_size(&size, text, font, letter_space, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    // Rounding in the label's own layout can make it a pixel wider than the measured text
    return size.x + 1;
}

int32_t measure_text_width(lv_obj_t* label, const lv_font_t* font) {
    return measure_width(lv_label_get_text(label), font, lv_obj_get_style_text_letter_space(label, LV_PART_MAIN));
}

/** The regular font is the tab bar's, as the tabs only have a font of their own when it's the small one */
void set_tab_text(lv_obj_t* tab, const char* text) {
    auto* label = get_label(tab);
    lv_label_set_text(label, text);
    auto* data = get_tab_data(tab);
    data->text_width = measure_text_width(label, lv_obj_get_style_text_font(lv_obj_get_parent(tab), LV_PART_MAIN));
    data->text_width_small = measure_text_width(label, lvgl_get_text_font(FONT_SIZE_SMALL));
}

/** The width of the tab when its label isn't shortened. The icon is above the label. */
int32_t get_natural_width(lv_obj_t* tab, bool small_font, bool icon_only) {
    const auto* data = get_tab_data(tab);
    const int32_t text_width = icon_only ? 0 : (small_font ? data->text_width_small : data->text_width);
    return std::max(text_width, data->icon_width) + lv_obj_get_style_pad_left(tab, LV_PART_MAIN) + lv_obj_get_style_pad_right(tab, LV_PART_MAIN);
}

/** Fixed tabs share the width equally, wrapped tabs together need the sum of their widths */
bool fits(const std::vector<lv_obj_t*>& shown, LvglTabsMode mode, int32_t available, bool small_font, bool icon_only) {
    const auto count = static_cast<int32_t>(shown.size());
    int32_t total = 0;
    for (auto* tab : shown) {
        const int32_t width = get_natural_width(tab, small_font, icon_only);
        if (mode == LVGL_TABS_MODE_FIXED && width > available / count) {
            return false;
        }
        total += width;
    }
    return mode != LVGL_TABS_MODE_WRAP || total <= available;
}

/**
 * The width that the tabs can take. In a row, the tab bar gets the room that the other widgets leave,
 * except for widgets that grow, as they take the room that the tab bar leaves.
 */
int32_t get_available_width(lv_obj_t* tabs) {
    auto* parent = lv_obj_get_parent(tabs);
    int32_t available = lv_obj_get_content_width(parent) - lv_obj_get_style_margin_left(tabs, LV_PART_MAIN) - lv_obj_get_style_margin_right(tabs, LV_PART_MAIN);
    const auto flow = lv_obj_get_style_flex_flow(parent, LV_PART_MAIN);
    const bool row = lv_obj_get_style_layout(parent, LV_PART_MAIN) == LV_LAYOUT_FLEX && (flow == LV_FLEX_FLOW_ROW || flow == LV_FLEX_FLOW_ROW_REVERSE);
    if (!row) {
        return available;
    }
    int32_t shown = 0;
    const uint32_t count = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < count; i++) {
        auto* child = lv_obj_get_child(parent, static_cast<int32_t>(i));
        if (!lv_obj_is_layout_positioned(child)) {
            continue;
        }
        shown++;
        if (child != tabs && lv_obj_get_style_flex_grow(child, LV_PART_MAIN) == 0) {
            available -= lv_obj_get_width(child) + lv_obj_get_style_margin_left(child, LV_PART_MAIN) + lv_obj_get_style_margin_right(child, LV_PART_MAIN);
        }
    }
    return available - std::max<int32_t>(0, shown - 1) * lv_obj_get_style_pad_column(parent, LV_PART_MAIN);
}

void set_small_font(lv_obj_t* tabs, bool small_font, bool icon_only) {
    auto* data = reinterpret_cast<Tabs*>(tabs);
    if (data->small_font == small_font && data->icon_only == icon_only) {
        return;
    }
    data->small_font = small_font;
    data->icon_only = icon_only;
    const uint32_t count = lv_obj_get_child_count(tabs);
    for (uint32_t i = 0; i < count; i++) {
        auto* tab = lv_obj_get_child(tabs, static_cast<int32_t>(i));
        if (small_font) {
            lv_obj_set_style_text_font(tab, lvgl_get_text_font(FONT_SIZE_SMALL), LV_PART_MAIN);
        } else {
            lv_obj_remove_local_style_prop(tab, LV_STYLE_TEXT_FONT, LV_PART_MAIN);
        }
        // The label's size follows the font
        fit_label(tab);
    }
}

/**
 * Tabs that don't fit with the regular font use the small one. When they don't fit with that either and they all have an
 * icon, they only show their icon. Otherwise only the labels that still don't fit are shortened.
 * Wrapped tabs are as wide as their label. When they don't fit, the narrow tabs keep their width and the wide ones share
 * the rest equally, so only the longest labels are shortened.
 */
void layout_tabs(lv_obj_t* tabs) {
    const auto mode = get_mode(tabs);
    if (mode == LVGL_TABS_MODE_SCROLLABLE) {
        set_small_font(tabs, false, false);
        return;
    }
    if (lv_obj_get_parent(tabs) == nullptr) {
        return;
    }
    const uint32_t count = lv_obj_get_child_count(tabs);
    std::vector<lv_obj_t*> shown;
    shown.reserve(count);
    for (uint32_t i = 0; i < count; i++) {
        auto* tab = lv_obj_get_child(tabs, static_cast<int32_t>(i));
        if (!lv_obj_is_hidden(tab)) {
            shown.push_back(tab);
        }
    }
    if (shown.empty()) {
        return;
    }
    const auto shown_count = static_cast<int32_t>(shown.size());
    const int32_t gaps = (shown_count - 1) * lv_obj_get_style_pad_column(tabs, LV_PART_MAIN);
    int32_t remaining = get_available_width(tabs) - lv_obj_get_style_pad_left(tabs, LV_PART_MAIN) - lv_obj_get_style_pad_right(tabs, LV_PART_MAIN) - gaps;

    const bool small_font = !fits(shown, mode, remaining, false, false);
    const bool all_have_icons = std::ranges::all_of(shown, [](lv_obj_t* tab) { return get_tab_data(tab)->icon != nullptr; });
    const bool icon_only = small_font && all_have_icons && !fits(shown, mode, remaining, true, false);
    set_small_font(tabs, small_font, icon_only);
    if (mode != LVGL_TABS_MODE_WRAP) {
        return;
    }

    std::vector<std::pair<int32_t, lv_obj_t*>> natural;
    natural.reserve(shown.size());
    for (auto* tab : shown) {
        natural.emplace_back(get_natural_width(tab, small_font, icon_only), tab);
    }
    std::ranges::sort(natural, {}, &std::pair<int32_t, lv_obj_t*>::first);
    int32_t left = shown_count;
    for (const auto& [width, tab] : natural) {
        const int32_t share = std::max<int32_t>(0, remaining / left);
        const int32_t assigned = std::min(width, share);
        lv_obj_set_width(tab, assigned);
        remaining -= assigned;
        left--;
    }
}

void on_parent_changed(lv_event_t* event) {
    layout_tabs(static_cast<lv_obj_t*>(lv_event_get_user_data(event)));
}

void on_tabs_deleted(lv_event_t* event) {
    auto* tabs = lv_event_get_current_target_obj(event);
    auto* parent = lv_obj_get_parent(tabs);
    if (parent != nullptr) {
        lv_obj_remove_event_cb_with_user_data(parent, on_parent_changed, tabs);
    }
}

void apply_tab_size(lv_obj_t* tab, LvglTabsMode mode) {
    switch (mode) {
        case LVGL_TABS_MODE_FIXED:
            lv_obj_set_flex_grow(tab, 1);
            lv_obj_set_width(tab, 0);
            break;
        case LVGL_TABS_MODE_SCROLLABLE:
            lv_obj_set_flex_grow(tab, 0);
            lv_obj_set_width(tab, LV_SIZE_CONTENT);
            break;
        case LVGL_TABS_MODE_WRAP:
            // The width is set by layout_tabs()
            lv_obj_set_flex_grow(tab, 0);
            break;
    }
    fit_label(tab);
}

void apply_mode(lv_obj_t* tabs) {
    const auto mode = get_mode(tabs);
    lv_obj_set_width(tabs, (mode == LVGL_TABS_MODE_WRAP) ? LV_SIZE_CONTENT : LV_PCT(100));
    lv_obj_set_scrollable(tabs, mode == LVGL_TABS_MODE_SCROLLABLE);
    if (mode != LVGL_TABS_MODE_SCROLLABLE) {
        lv_obj_scroll_to_x(tabs, 0, LV_ANIM_OFF);
    }
    const uint32_t count = lv_obj_get_child_count(tabs);
    for (uint32_t i = 0; i < count; i++) {
        apply_tab_size(lv_obj_get_child(tabs, static_cast<int32_t>(i)), mode);
    }
    layout_tabs(tabs);
}

void select_tab(lv_obj_t* tabs, lv_obj_t* selected) {
    const uint32_t count = lv_obj_get_child_count(tabs);
    for (uint32_t i = 0; i < count; i++) {
        auto* tab = lv_obj_get_child(tabs, static_cast<int32_t>(i));
        lv_obj_set_state(tab, LV_STATE_CHECKED, tab == selected);
    }
    if (get_mode(tabs) == LVGL_TABS_MODE_SCROLLABLE) {
        lv_obj_scroll_to_view(selected, LV_ANIM_ON);
    }
}

void on_tab_clicked(lv_event_t* event) {
    auto* tab = lv_event_get_current_target_obj(event);
    auto* tabs = lv_obj_get_parent(tab);
    if (lv_obj_has_state(tab, LV_STATE_CHECKED)) {
        return;
    }
    select_tab(tabs, tab);
    lv_obj_send_event(tabs, LV_EVENT_VALUE_CHANGED, nullptr);
}

}

lv_obj_t* lvgl_tabs_create(lv_obj_t* parent) {
    lv_obj_t* obj = lv_obj_class_create_obj(&lvgl_tabs_class, parent);
    lv_obj_class_init_obj(obj);
    lv_obj_set_size(obj, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_scrollable(obj, false);
    lv_obj_set_scroll_dir(obj, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lvgl_grid_navigation_add(obj);
    // Wrapped tabs are shortened when the parent gets too narrow for them, or when a widget next to them is shown
    lv_obj_add_event_cb(parent, on_parent_changed, LV_EVENT_SIZE_CHANGED, obj);
    lv_obj_add_event_cb(parent, on_parent_changed, LV_EVENT_LAYOUT_CHANGED, obj);
    lv_obj_add_event_cb(obj, on_tabs_deleted, LV_EVENT_DELETE, nullptr);
    return obj;
}

lv_obj_t* lvgl_tabs_add_tab(lv_obj_t* tabs, const char* text) {
    return lvgl_tabs_add_tab_with_icon(tabs, nullptr, text);
}

lv_obj_t* lvgl_tabs_add_tab_with_icon(lv_obj_t* tabs, const char* icon, const char* text) {
    lv_obj_t* tab = lv_obj_class_create_obj(&lvgl_tabs_tab_class, tabs);
    lv_obj_class_init_obj(tab);
    auto* data = get_tab_data(tab);
    lv_obj_set_height(tab, LV_SIZE_CONTENT);
    // The icon is above the label, both centered
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(tab, on_tab_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(tab, on_tab_size_changed, LV_EVENT_SIZE_CHANGED, nullptr);
    if (icon != nullptr) {
        data->icon = lv_label_create(tab);
        const lv_font_t* icon_font = lvgl_get_shared_icon_default_font();
        lv_obj_set_style_text_font(data->icon, icon_font, LV_STATE_DEFAULT);
        lv_label_set_text(data->icon, icon);
        data->icon_width = measure_width(icon, icon_font, 0);
    }
    data->label = lv_label_create(tab);
    set_tab_text(tab, text);
    lv_obj_set_style_text_align(data->label, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
    if (reinterpret_cast<Tabs*>(tabs)->small_font) {
        lv_obj_set_style_text_font(tab, lvgl_get_text_font(FONT_SIZE_SMALL), LV_PART_MAIN);
    }
    apply_tab_size(tab, get_mode(tabs));
    layout_tabs(tabs);
    if (lv_obj_get_child_count(tabs) == 1) {
        lv_obj_add_state(tab, LV_STATE_CHECKED);
    }
    return tab;
}

void lvgl_tabs_set_tab_text(lv_obj_t* tabs, uint32_t index, const char* text) {
    auto* tab = lv_obj_get_child(tabs, static_cast<int32_t>(index));
    if (tab == nullptr) {
        return;
    }
    set_tab_text(tab, text);
    layout_tabs(tabs);
}

void lvgl_tabs_set_tab_badge(lv_obj_t* tabs, uint32_t index, bool visible) {
    auto* tab = lv_obj_get_child(tabs, static_cast<int32_t>(index));
    if (tab == nullptr) {
        return;
    }
    auto* data = get_tab_data(tab);
    if (!visible) {
        if (data->badge != nullptr) {
            lv_obj_delete(data->badge);
            data->badge = nullptr;
        }
        return;
    }
    if (data->badge != nullptr) {
        return;
    }
    auto* badge = lvgl_badge_create(tab);
    data->badge = badge;
    // At the top of the label, in the padding at the end of the tab, so it never changes the size of the tab
    const int32_t size = lv_obj_get_style_width(badge, LV_PART_MAIN);
    lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, size + size / 2, 0);
}

void lvgl_tabs_set_tab_hidden(lv_obj_t* tabs, uint32_t index, bool hidden) {
    auto* tab = lv_obj_get_child(tabs, static_cast<int32_t>(index));
    if (tab == nullptr || lv_obj_is_hidden(tab) == hidden) {
        return;
    }
    lv_obj_set_hidden(tab, hidden);
    if (hidden && lv_obj_has_state(tab, LV_STATE_CHECKED)) {
        const uint32_t count = lv_obj_get_child_count(tabs);
        for (uint32_t i = 0; i < count; i++) {
            auto* candidate = lv_obj_get_child(tabs, static_cast<int32_t>(i));
            if (!lv_obj_is_hidden(candidate)) {
                select_tab(tabs, candidate);
                break;
            }
        }
    }
    layout_tabs(tabs);
}

void lvgl_tabs_set_mode(lv_obj_t* tabs, LvglTabsMode mode) {
    reinterpret_cast<Tabs*>(tabs)->mode = mode;
    apply_mode(tabs);
}

void lvgl_tabs_set_selected(lv_obj_t* tabs, uint32_t index) {
    auto* tab = lv_obj_get_child(tabs, static_cast<int32_t>(index));
    if (tab != nullptr) {
        select_tab(tabs, tab);
    }
}

uint32_t lvgl_tabs_get_selected(lv_obj_t* tabs) {
    const uint32_t count = lv_obj_get_child_count(tabs);
    for (uint32_t i = 0; i < count; i++) {
        if (lv_obj_has_state(lv_obj_get_child(tabs, static_cast<int32_t>(i)), LV_STATE_CHECKED)) {
            return i;
        }
    }
    return 0;
}
