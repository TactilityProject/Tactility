// SPDX-License-Identifier: Apache-2.0
#define LV_USE_PRIVATE_API 1 // For the lv_obj_class_t declaration and lv_obj_t's layout

#include <lvgl/widgets/path_bar.h>

#include <lvgl/grid_navigation.h>
#include <lvgl/theme.h>
#include <lvgl/widgets/pathbar_button.h>

#include <algorithm>
#include <new>
#include <string>
#include <vector>

namespace {

struct PathBar {
    lv_obj_t* obj = nullptr;
    lv_obj_t* toolbar = nullptr;
    LvglPathBarPressedFn onPressed = nullptr;
    LvglPathBarRebuiltFn onRebuilt = nullptr;
    void* userData = nullptr;
    std::string path;
    bool hasPath = false;
    /** The widgets in the toolbar: the segment buttons that fit, and an ellipsis in front when the path is cut off */
    std::vector<lv_obj_t*> widgets;
    /** The path that each segment navigates to, by the index in the segment's user data */
    std::vector<std::string> paths;
    /** The toolbar width that the segments were built for */
    int32_t builtWidth = 0;
};

struct PathBarObject {
    lv_obj_t obj;
    PathBar* bar;
};

// Not themed: the path bar is a hidden holder of the state, the segments are in the toolbar
const lv_obj_class_t lvgl_path_bar_class = {
    .base_class = &lv_obj_class,
    .constructor_cb = nullptr,
    .destructor_cb = nullptr,
    .event_cb = nullptr,
    .user_data = nullptr,
    .name = "path_bar",
    .width_def = 0,
    .height_def = 0,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_FALSE,
    .instance_size = sizeof(PathBarObject),
    .theme_inheritable = LV_OBJ_CLASS_THEME_INHERITABLE_FALSE
};

PathBar* getBar(lv_obj_t* obj) {
    if (obj == nullptr || !lv_obj_check_type(obj, &lvgl_path_bar_class)) {
        return nullptr;
    }
    return reinterpret_cast<PathBarObject*>(obj)->bar;
}

void onSegmentPressed(lv_event_t* event) {
    auto* self = static_cast<PathBar*>(lv_event_get_user_data(event));
    lv_obj_t* segment = lv_event_get_target_obj(event);
    const auto index = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(segment));
    if (index < self->paths.size() && self->onPressed != nullptr) {
        // Copied, as the callback can rebuild the segments
        const std::string path = self->paths[index];
        // Only keys show a selection, a touch doesn't
        const bool fromKeys = lv_obj_has_state(segment, LV_STATE_FOCUS_KEY);
        self->onPressed(self->obj, path.c_str(), fromKeys, self->userData);
    }
}

void clearWidgets(PathBar* self) {
    for (auto* widget : self->widgets) {
        lv_obj_delete(widget);
    }
    self->widgets.clear();
}

lv_obj_t* createSegment(PathBar* self, const std::string& name, size_t index, bool current) {
    auto* segment = lvgl_pathbar_button_create(self->toolbar);
    // The button fits the height of the toolbar, with its focus ring inside, as the toolbar clips what is outside of it
    lv_obj_set_style_margin_ver(segment, 0, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(segment, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(segment, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_state(segment, LV_STATE_FOCUS_KEY);
    const int32_t ring_width = lv_obj_get_style_outline_width(segment, LV_PART_MAIN);
    lv_obj_remove_state(segment, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_pad(segment, -ring_width, LV_STATE_FOCUS_KEY);
    if (lvgl_theme_is_mono()) {
        // The ring has the colour of the chip's border, so the selected chip is inverted instead
        lv_obj_set_style_bg_color(segment, lv_obj_get_style_text_color(segment, LV_PART_MAIN), LV_STATE_FOCUS_KEY);
        lv_obj_set_style_bg_opa(segment, LV_OPA_COVER, LV_STATE_FOCUS_KEY);
        lv_obj_set_style_text_color(segment, lv_obj_get_style_bg_color(segment, LV_PART_MAIN), LV_STATE_FOCUS_KEY);
    }
    lv_obj_t* label = lv_label_create(segment);
    lv_label_set_text(label, name.c_str());
    // One line with the width of the name. A content size would make the label wrap to the room that is left.
    lv_point_t text_size;
    lv_text_get_size(&text_size, name.c_str(), lv_obj_get_style_text_font(label, LV_PART_MAIN), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(label, text_size.x, text_size.y);
    lv_obj_set_user_data(segment, reinterpret_cast<void*>(index));
    if (current) {
        lv_obj_add_state(segment, LV_STATE_DISABLED);
        lv_obj_set_click_focusable(segment, false);
    } else {
        lv_obj_add_event_cb(segment, onSegmentPressed, LV_EVENT_SHORT_CLICKED, self);
    }
    return segment;
}

// How much wider the visible children of the toolbar are than the toolbar, with the gaps between them. 0 or less when they fit.
int32_t getOverflow(lv_obj_t* toolbar) {
    lv_obj_update_layout(toolbar);
    int32_t used = 0;
    int32_t count = 0;
    const uint32_t child_count = lv_obj_get_child_count(toolbar);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t* child = lv_obj_get_child(toolbar, static_cast<int32_t>(i));
        if (!lv_obj_is_hidden(child)) {
            // The margin of a chip, for its focus ring, takes room too
            used += lv_obj_get_width(child) + lv_obj_get_style_margin_left(child, LV_PART_MAIN) + lv_obj_get_style_margin_right(child, LV_PART_MAIN);
            count++;
        }
    }
    used += (count - 1) * lv_obj_get_style_pad_column(toolbar, LV_PART_MAIN);
    return used - lv_obj_get_content_width(toolbar);
}

void rebuild(PathBar* self) {
    clearWidgets(self);
    self->paths.clear();
    if (!self->hasPath) {
        return;
    }

    // The root, and a path for every directory of the current path
    std::vector<std::string> names = { "/" };
    self->paths.emplace_back("/");
    const auto& current_path = self->path;
    std::string accumulated;
    size_t start = 0;
    while (start < current_path.size()) {
        if (current_path[start] == '/') {
            start++;
            continue;
        }
        size_t end = current_path.find('/', start);
        if (end == std::string::npos) {
            end = current_path.size();
        }
        names.push_back(current_path.substr(start, end - start));
        accumulated += "/" + names.back();
        self->paths.push_back(accumulated);
        start = end;
    }

    lv_obj_update_layout(self->toolbar);

    // Shows the segments from the first one that is given, after the navigation button of the toolbar,
    // with an ellipsis in front of the first one when the path is cut off
    auto build = [&](size_t first) {
        int32_t index = 1;
        if (first > 0) {
            auto* ellipsis = lv_label_create(self->toolbar);
            lv_label_set_text(ellipsis, "..");
            lv_obj_move_to_index(ellipsis, index++);
            self->widgets.push_back(ellipsis);
        }
        for (size_t i = first; i < names.size(); i++) {
            lv_obj_t* segment = createSegment(self, names[i], i, i + 1 == names.size());
            lv_obj_move_to_index(segment, index++);
            self->widgets.push_back(segment);
        }
    };

    size_t first = 0;
    build(first);
    // Drops the leftmost segments until the path fits, keeping at least the current directory and its parent
    while (getOverflow(self->toolbar) > 0 && first + 2 < names.size()) {
        clearWidgets(self);
        first++;
        build(first);
    }
    // Shortens the name of the current directory when that is still not enough
    auto shorten = [self](lv_obj_t* segment) {
        const int32_t excess = getOverflow(self->toolbar);
        if (excess > 0) {
            lv_obj_t* label = lv_obj_get_child(segment, 0);
            lv_obj_set_width(label, std::max<int32_t>(lv_font_get_line_height(lv_obj_get_style_text_font(label, LV_PART_MAIN)), lv_obj_get_width(label) - excess));
        }
    };
    shorten(self->widgets.back());
    // Then the name of the parent, which is left out when even that is not enough
    if (names.size() >= 2) {
        const auto parent = self->widgets.end() - 2;
        shorten(*parent);
        if (getOverflow(self->toolbar) > 0) {
            lv_obj_delete(*parent);
            self->widgets.erase(parent);
        }
    }
    self->builtWidth = lv_obj_get_width(self->toolbar);

    if (self->onRebuilt != nullptr) {
        self->onRebuilt(self->obj, self->userData);
    }
}

void onDeferredRebuild(void* userData) {
    rebuild(static_cast<PathBar*>(userData));
}

// The toolbar can change size when the system bars change. The segments are rebuilt after the layout pass.
void onToolbarSizeChanged(lv_event_t* event) {
    auto* self = static_cast<PathBar*>(lv_event_get_user_data(event));
    if (lv_obj_get_width(self->toolbar) != self->builtWidth) {
        lv_async_call_cancel(onDeferredRebuild, self);
        lv_async_call(onDeferredRebuild, self);
    }
}

void onDeleted(lv_event_t* event) {
    auto* self = static_cast<PathBar*>(lv_event_get_user_data(event));
    lv_async_call_cancel(onDeferredRebuild, self);
    lv_obj_remove_event_cb_with_user_data(self->toolbar, onToolbarSizeChanged, self);
    // The segments call back into the bar. A toolbar that is being deleted deletes them itself.
    if (!self->toolbar->is_deleting) {
        clearWidgets(self);
    }
    reinterpret_cast<PathBarObject*>(self->obj)->bar = nullptr;
    delete self;
}

} // namespace

extern "C" {

lv_obj_t* lvgl_path_bar_create(lv_obj_t* toolbar, LvglPathBarPressedFn on_pressed, LvglPathBarRebuiltFn on_rebuilt, void* user_data) {
    auto* self = new (std::nothrow) PathBar();
    if (self == nullptr) {
        return nullptr;
    }
    self->toolbar = toolbar;
    self->onPressed = on_pressed;
    self->onRebuilt = on_rebuilt;
    self->userData = user_data;

    lv_obj_t* obj = lv_obj_class_create_obj(&lvgl_path_bar_class, toolbar);
    lv_obj_class_init_obj(obj);
    reinterpret_cast<PathBarObject*>(obj)->bar = self;
    self->obj = obj;
    // Takes no room and no input
    lv_obj_set_hidden(obj, true);
    lv_obj_set_clickable(obj, false);
    lv_obj_add_event_cb(obj, onDeleted, LV_EVENT_DELETE, self);
    lv_obj_add_event_cb(toolbar, onToolbarSizeChanged, LV_EVENT_SIZE_CHANGED, self);
    return obj;
}

void lvgl_path_bar_set_path(lv_obj_t* bar, const char* path) {
    PathBar* self = getBar(bar);
    if (self == nullptr || path == nullptr) {
        return;
    }
    self->path = path;
    self->hasPath = true;
    rebuild(self);
}

lv_obj_t* lvgl_path_bar_get_parent_segment(lv_obj_t* bar) {
    PathBar* self = getBar(bar);
    if (self == nullptr || self->widgets.empty()) {
        return nullptr;
    }
    const auto& widgets = self->widgets;
    if (widgets.size() >= 2 && lv_obj_is_clickable(widgets[widgets.size() - 2])) {
        return widgets[widgets.size() - 2];
    }
    return widgets.back();
}

void lvgl_path_bar_focus_parent_segment(lv_obj_t* bar) {
    PathBar* self = getBar(bar);
    lv_obj_t* segment = lvgl_path_bar_get_parent_segment(bar);
    if (self == nullptr || segment == nullptr) {
        return;
    }
    lv_group_focus_obj(self->toolbar);
    lv_gridnav_set_focused(self->toolbar, segment, LV_ANIM_OFF);
}

} // extern "C"
