// SPDX-License-Identifier: Apache-2.0
#include <lvgl/grid_navigation.h>

static void remove_from_group(lv_obj_t* child) {
    if (lv_obj_get_group(child) != nullptr) {
        lv_group_remove_obj(child);
    }
}

// Children join the default group when they're created, but grid navigation focuses them through the container
static void on_child_created(lv_event_t* event) {
    auto* child = static_cast<lv_obj_t*>(lv_event_get_param(event));
    auto* container = lv_event_get_current_target_obj(event);
    if (child != nullptr && lv_obj_get_parent(child) == container) {
        remove_from_group(child);
    }
}

// Matches which children grid navigation can focus
static bool is_focusable(lv_obj_t* obj) {
    return !lv_obj_is_hidden(obj) && lv_obj_is_clickable(obj) && lv_obj_is_click_focusable(obj);
}

// Finds the next focusable child after (or before) the given index
static lv_obj_t* find_focusable_child(lv_obj_t* container, int32_t index, bool next) {
    const auto child_count = static_cast<int32_t>(lv_obj_get_child_count(container));
    for (index += next ? 1 : -1; index >= 0 && index < child_count; index += next ? 1 : -1) {
        auto* child = lv_obj_get_child(container, index);
        if (is_focusable(child)) {
            return child;
        }
    }
    return nullptr;
}

static int32_t get_center_x(lv_obj_t* obj) {
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    return area.x1 + lv_area_get_width(&area) / 2;
}

static int32_t get_center_y(lv_obj_t* obj) {
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    return area.y1 + lv_area_get_height(&area) / 2;
}

// Finds the child in the top (or bottom) row that's horizontally closest to the reference child.
// The reference child is returned when it's in that row already.
static lv_obj_t* find_edge_row_child(lv_obj_t* container, bool top, lv_obj_t* reference) {
    const uint32_t child_count = lv_obj_get_child_count(container);
    bool found = false;
    int32_t edge_y = 0;
    for (uint32_t i = 0; i < child_count; i++) {
        auto* child = lv_obj_get_child(container, static_cast<int32_t>(i));
        if (is_focusable(child)) {
            const int32_t y = get_center_y(child);
            if (!found || (top ? y < edge_y : y > edge_y)) {
                edge_y = y;
                found = true;
            }
        }
    }
    if (!found) {
        return nullptr;
    }
    if (reference != nullptr && get_center_y(reference) == edge_y) {
        return reference;
    }
    const int32_t reference_x = reference != nullptr ? get_center_x(reference) : 0;
    lv_obj_t* closest = nullptr;
    int32_t closest_distance = 0;
    for (uint32_t i = 0; i < child_count; i++) {
        auto* child = lv_obj_get_child(container, static_cast<int32_t>(i));
        if (is_focusable(child) && get_center_y(child) == edge_y) {
            const int32_t distance = LV_ABS(get_center_x(child) - reference_x);
            if (closest == nullptr || distance < closest_distance) {
                closest = child;
                closest_distance = distance;
            }
        }
    }
    return closest;
}

// Grid navigation only scrolls the container itself, which isn't scrollable when e.g. it's part of a scrolling page
static void scroll_to_focused_child(lv_obj_t* container) {
    lv_obj_t* child = lvgl_grid_navigation_get_focused(container);
    if (child != nullptr) {
        lv_obj_scroll_to_view_recursive(child, LV_ANIM_ON);
    }
}

static void on_container_key(lv_event_t* event) {
    scroll_to_focused_child(lv_event_get_current_target_obj(event));
}

// Grid navigation shows the focused child as selected whenever the container gets the focus. That's only
// right when a key moved the focus there, not when it moved because e.g. the previously focused widget was deleted.
static void on_container_focused(lv_event_t* event) {
    lv_indev_t* indev = lv_indev_active();
    const lv_indev_type_t type = indev != nullptr ? lv_indev_get_type(indev) : LV_INDEV_TYPE_NONE;
    lv_obj_t* container = lv_event_get_current_target_obj(event);
    if (type == LV_INDEV_TYPE_KEYPAD) {
        // Grid navigation restores the child that was focused last, but keys enter at the side they came from:
        // up and down enter at the bottom or top row, the other keys at the last or first child
        const uint32_t key = lv_indev_get_key(indev);
        lv_obj_t* focused = lvgl_grid_navigation_get_focused(container);
        lv_obj_t* child = nullptr;
        if (key == LV_KEY_DOWN || key == LV_KEY_UP) {
            child = find_edge_row_child(container, key == LV_KEY_DOWN, focused);
        } else if (key == LV_KEY_NEXT || key == LV_KEY_RIGHT) {
            child = find_focusable_child(container, -1, true);
        } else if (key == LV_KEY_PREV || key == LV_KEY_LEFT) {
            child = find_focusable_child(container, static_cast<int32_t>(lv_obj_get_child_count(container)), false);
        }
        if (child != nullptr && child != focused) {
            lv_gridnav_set_focused(container, child, LV_ANIM_ON);
        }
        scroll_to_focused_child(container);
        return;
    }
    if (type == LV_INDEV_TYPE_ENCODER) {
        scroll_to_focused_child(container);
        return;
    }
    lv_obj_t* child = lvgl_grid_navigation_get_focused(container);
    if (child != nullptr) {
        lv_obj_remove_state(child, LV_STATE_FOCUS_KEY);
    }
}

void lvgl_grid_navigation_add(lv_obj_t* container) {
    lv_gridnav_add(container, LV_GRIDNAV_CTRL_NONE);
    // Added after grid navigation's own handler, so it runs after it
    lv_obj_add_event_cb(container, on_container_focused, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(container, on_container_key, LV_EVENT_KEY, nullptr);

    const uint32_t child_count = lv_obj_get_child_count(container);
    for (uint32_t i = 0; i < child_count; i++) {
        remove_from_group(lv_obj_get_child(container, static_cast<int32_t>(i)));
    }
    lv_obj_add_event_cb(container, on_child_created, LV_EVENT_CHILD_CREATED, nullptr);

    auto* group = lv_group_get_default();
    if (group != nullptr && lv_obj_get_group(container) == nullptr) {
        lv_group_add_obj(group, container);
    }
}

void lvgl_grid_navigation_remove(lv_obj_t* container) {
    if (!lvgl_grid_navigation_is_container(container)) {
        return;
    }
    lv_obj_t* focused_child = lvgl_grid_navigation_get_focused(container);
    lv_group_t* group = lv_obj_get_group(container);
    const bool container_focused = group != nullptr && lv_group_get_focused(group) == container;

    lv_gridnav_remove(container);
    lv_obj_remove_event_cb(container, on_child_created);
    lv_obj_remove_event_cb(container, on_container_focused);
    lv_obj_remove_event_cb(container, on_container_key);

    if (group == nullptr) {
        return;
    }
    // The children are added at the end of the group
    const uint32_t child_count = lv_obj_get_child_count(container);
    for (uint32_t i = 0; i < child_count; i++) {
        auto* child = lv_obj_get_child(container, static_cast<int32_t>(i));
        if (lv_obj_is_group_def(child) && lv_obj_get_group(child) == nullptr) {
            lv_group_add_obj(group, child);
        }
    }
    if (container_focused && focused_child != nullptr) {
        lv_group_focus_obj(focused_child);
    }
}

void lvgl_grid_navigation_step(lv_group_t* group, bool next) {
    lv_obj_t* focused = lv_group_get_focused(group);
    if (lvgl_grid_navigation_is_container(focused)) {
        lv_obj_t* focused_child = lvgl_grid_navigation_get_focused(focused);
        const int32_t index = focused_child != nullptr ? lv_obj_get_index(focused_child) : (next ? -1 : static_cast<int32_t>(lv_obj_get_child_count(focused)));
        lv_obj_t* child = find_focusable_child(focused, index, next);
        if (child != nullptr) {
            lv_gridnav_set_focused(focused, child, LV_ANIM_ON);
            scroll_to_focused_child(focused);
            return;
        }
    }

    lv_group_set_editing(group, false);
    if (next) {
        lv_group_focus_next(group);
    } else {
        lv_group_focus_prev(group);
    }

    // Grid navigation restores the child that was focused last, but stepping enters at the side it came from
    lv_obj_t* entered = lv_group_get_focused(group);
    if (lvgl_grid_navigation_is_container(entered)) {
        const int32_t start = next ? -1 : static_cast<int32_t>(lv_obj_get_child_count(entered));
        lv_obj_t* child = find_focusable_child(entered, start, next);
        if (child != nullptr) {
            lv_gridnav_set_focused(entered, child, LV_ANIM_ON);
            scroll_to_focused_child(entered);
        }
    }
}

void lvgl_focus_hide_key_selection(lv_group_t* group) {
    lv_obj_t* focused = lv_group_get_focused(group);
    if (focused == nullptr) {
        return;
    }
    lv_group_set_editing(group, false);
    constexpr auto key_states = static_cast<lv_state_t>(LV_STATE_FOCUS_KEY | LV_STATE_EDITED);
    lv_obj_remove_state(focused, key_states);
    if (lvgl_grid_navigation_is_container(focused)) {
        lv_obj_t* child = lvgl_grid_navigation_get_focused(focused);
        if (child != nullptr) {
            lv_obj_remove_state(child, key_states);
        }
    }
}

lv_obj_t* lvgl_grid_navigation_get_focused(lv_obj_t* container) {
    // Grid navigation marks the focused child with the focused state
    const uint32_t child_count = lv_obj_get_child_count(container);
    for (uint32_t i = 0; i < child_count; i++) {
        auto* child = lv_obj_get_child(container, static_cast<int32_t>(i));
        if (lv_obj_has_state(child, LV_STATE_FOCUSED)) {
            return child;
        }
    }
    return nullptr;
}

bool lvgl_grid_navigation_is_container(const lv_obj_t* obj) {
    if (obj == nullptr) {
        return false;
    }
    // Grid navigation containers are the objects with this module's focus handler
    auto* container = const_cast<lv_obj_t*>(obj);
    const uint32_t event_count = lv_obj_get_event_count(container);
    for (uint32_t i = 0; i < event_count; i++) {
        if (lv_event_dsc_get_cb(lv_obj_get_event_dsc(container, i)) == on_container_focused) {
            return true;
        }
    }
    return false;
}
