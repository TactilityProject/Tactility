// SPDX-License-Identifier: Apache-2.0
#define LV_USE_PRIVATE_API 1 // For the lv_obj_class_t declaration

#include <lvgl/widgets/page_indicator.h>

#include <cstdio>

static void refresh(lv_obj_t* obj);

static void on_style_changed(lv_event_t* event) {
    refresh(lv_event_get_current_target_obj(event));
}

static void page_indicator_constructor(const lv_obj_class_t* class_p, lv_obj_t* obj) {
    LV_UNUSED(class_p);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    // Shows the page numbers instead of the dots when they don't fit in the indicator's maximum width
    lv_obj_t* label = lv_label_create(obj);
    // One line, so the indicator's height doesn't change with its width
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    // A change of the maximum width can switch between dots and numbers
    lv_obj_add_event_cb(obj, on_style_changed, LV_EVENT_STYLE_CHANGED, nullptr);
}

static void page_indicator_dot_constructor(const lv_obj_class_t* class_p, lv_obj_t* obj) {
    LV_UNUSED(class_p);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

// Not static: the theme styles page indicators and their dots by these classes
extern "C" const lv_obj_class_t lvgl_page_indicator_class = {
    .base_class = &lv_obj_class,
    .constructor_cb = page_indicator_constructor,
    .destructor_cb = nullptr,
    .event_cb = nullptr,
    .user_data = nullptr,
    .name = "page_indicator",
    .width_def = LV_SIZE_CONTENT,
    .height_def = LV_SIZE_CONTENT,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_FALSE,
    .instance_size = 0,
    .theme_inheritable = LV_OBJ_CLASS_THEME_INHERITABLE_FALSE
};

extern "C" const lv_obj_class_t lvgl_page_indicator_dot_class = {
    .base_class = &lv_obj_class,
    .constructor_cb = page_indicator_dot_constructor,
    .destructor_cb = nullptr,
    .event_cb = nullptr,
    .user_data = nullptr,
    .name = "page_indicator_dot",
    .width_def = 0,
    .height_def = 0,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_FALSE,
    .instance_size = 0,
    .theme_inheritable = LV_OBJ_CLASS_THEME_INHERITABLE_FALSE
};

// The first child is the label, the dots follow
static lv_obj_t* get_label(lv_obj_t* obj) {
    return lv_obj_get_child(obj, 0);
}

static uint32_t get_dot_count(lv_obj_t* obj) {
    return lv_obj_get_child_count(obj) - 1;
}

static lv_obj_t* get_dot(lv_obj_t* obj, uint32_t index) {
    return lv_obj_get_child(obj, static_cast<int32_t>(index + 1));
}

static void refresh(lv_obj_t* obj) {
    const uint32_t count = get_dot_count(obj);
    if (count == 0) {
        return;
    }
    const int32_t dot_width = lv_obj_get_style_width(get_dot(obj, 0), LV_PART_MAIN);
    const int32_t gap = lv_obj_get_style_pad_column(obj, LV_PART_MAIN);
    const int32_t dots_width = static_cast<int32_t>(count) * dot_width + static_cast<int32_t>(count - 1) * gap;
    const bool show_numbers = dots_width > lv_obj_get_style_max_width(obj, LV_PART_MAIN);

    lv_obj_t* label = get_label(obj);
    lv_obj_set_flag(label, LV_OBJ_FLAG_HIDDEN, !show_numbers);
    uint32_t page = 0;
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t* dot = get_dot(obj, i);
        lv_obj_set_flag(dot, LV_OBJ_FLAG_HIDDEN, show_numbers);
        if (lv_obj_has_state(dot, LV_STATE_CHECKED)) {
            page = i;
        }
    }
    if (show_numbers) {
        char text[24];
        std::snprintf(text, sizeof(text), "%lu/%lu", static_cast<unsigned long>(page + 1), static_cast<unsigned long>(count));
        lv_label_set_text(label, text);
    }
}

extern "C" {

lv_obj_t* lvgl_page_indicator_create(lv_obj_t* parent) {
    lv_obj_t* obj = lv_obj_class_create_obj(&lvgl_page_indicator_class, parent);
    lv_obj_class_init_obj(obj);
    return obj;
}

void lvgl_page_indicator_set_page_count(lv_obj_t* obj, uint32_t count) {
    uint32_t current = get_dot_count(obj);
    while (current > count) {
        lv_obj_delete(lv_obj_get_child(obj, -1));
        current--;
    }
    while (current < count) {
        lv_obj_t* dot = lv_obj_class_create_obj(&lvgl_page_indicator_dot_class, obj);
        lv_obj_class_init_obj(dot);
        current++;
    }
    refresh(obj);
}

void lvgl_page_indicator_set_page(lv_obj_t* obj, uint32_t page) {
    const uint32_t count = get_dot_count(obj);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_set_state(get_dot(obj, i), LV_STATE_CHECKED, i == page);
    }
    refresh(obj);
}

}
