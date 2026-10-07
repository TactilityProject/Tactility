// SPDX-License-Identifier: Apache-2.0
#include <lvgl/grid_navigation.h>

// Marks containers with grid navigation, so the keyboard sends them up and down keys
constexpr lv_obj_flag_t GRID_NAVIGATION_FLAG = LV_OBJ_FLAG_USER_1;

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

void lvgl_grid_navigation_add(lv_obj_t* container) {
    lv_gridnav_add(container, LV_GRIDNAV_CTRL_NONE);
    lv_obj_add_flag(container, GRID_NAVIGATION_FLAG);

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
    lv_obj_remove_flag(container, GRID_NAVIGATION_FLAG);

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

// Matches which children grid navigation can focus
static bool is_focusable(lv_obj_t* obj) {
    return !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) && lv_obj_has_flag_any(obj, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE));
}

void lvgl_grid_navigation_focus_next(lv_obj_t* container, bool next) {
    const auto child_count = static_cast<int32_t>(lv_obj_get_child_count(container));
    lv_obj_t* focused = lvgl_grid_navigation_get_focused(container);
    int32_t index = focused != nullptr ? lv_obj_get_index(focused) : (next ? -1 : child_count);
    for (index += next ? 1 : -1; index >= 0 && index < child_count; index += next ? 1 : -1) {
        auto* child = lv_obj_get_child(container, index);
        if (is_focusable(child)) {
            lv_gridnav_set_focused(container, child, LV_ANIM_ON);
            return;
        }
    }

    lv_group_t* group = lv_obj_get_group(container);
    if (group != nullptr) {
        if (next) {
            lv_group_focus_next(group);
        } else {
            lv_group_focus_prev(group);
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
    return obj != nullptr && lv_obj_has_flag(obj, GRID_NAVIGATION_FLAG);
}
