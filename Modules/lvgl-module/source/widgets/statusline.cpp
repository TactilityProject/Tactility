// SPDX-License-Identifier: Apache-2.0
#define LV_USE_PRIVATE_API 1 // For the lv_obj_class_t declaration

#include <lvgl/widgets/statusline.h>

#include <lvgl/fonts.h>

// Not static: the theme styles the statusline and its dot by these classes. The level is a state of the dot.
extern "C" const lv_obj_class_t lvgl_statusline_class = {
    .base_class = &lv_obj_class,
    .constructor_cb = nullptr,
    .destructor_cb = nullptr,
    .event_cb = nullptr,
    .user_data = nullptr,
    .name = "statusline",
    .width_def = 0,
    .height_def = 0,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_INHERIT,
    .instance_size = 0,
    .theme_inheritable = LV_OBJ_CLASS_THEME_INHERITABLE_FALSE
};

extern "C" const lv_obj_class_t lvgl_statusline_dot_class = {
    .base_class = &lv_obj_class,
    .constructor_cb = nullptr,
    .destructor_cb = nullptr,
    .event_cb = nullptr,
    .user_data = nullptr,
    .name = "statusline_dot",
    .width_def = 0,
    .height_def = 0,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_INHERIT,
    .instance_size = 0,
    .theme_inheritable = LV_OBJ_CLASS_THEME_INHERITABLE_FALSE
};

namespace {

constexpr int32_t DOT_INDEX = 0;
constexpr int32_t LABEL_INDEX = 1;

/** The dot shows the warning level with LV_STATE_USER_1 and the error level with LV_STATE_USER_2 */
constexpr lv_state_t WARNING_STATE = LV_STATE_USER_1;
constexpr lv_state_t ERROR_STATE = LV_STATE_USER_2;

lv_obj_t* create_object(const lv_obj_class_t* object_class, lv_obj_t* parent) {
    lv_obj_t* obj = lv_obj_class_create_obj(object_class, parent);
    lv_obj_class_init_obj(obj);
    return obj;
}

}

lv_obj_t* lvgl_statusline_create(lv_obj_t* parent) {
    lv_obj_t* obj = create_object(&lvgl_statusline_class, parent);
    lv_obj_set_size(obj, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(obj, false);
    lv_obj_set_clickable(obj, false);

    lv_obj_t* dot = create_object(&lvgl_statusline_dot_class, obj);
    lv_obj_set_scrollable(dot, false);
    lv_obj_set_clickable(dot, false);

    lv_obj_t* label = lv_label_create(obj);
    const lv_font_t* font = lvgl_get_text_font(FONT_SIZE_SMALL);
    lv_obj_set_style_text_font(label, font, LV_STATE_DEFAULT);
    // A fixed height keeps the text on one line, as it would wrap otherwise
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(label, 0, lv_font_get_line_height(font));
    lv_obj_set_flex_grow(label, 1);
    lv_label_set_text(label, "");
    return obj;
}

void lvgl_statusline_set_text(lv_obj_t* obj, const char* text) {
    lv_label_set_text(lv_obj_get_child(obj, LABEL_INDEX), text);
}

void lvgl_statusline_set_level(lv_obj_t* obj, LvglStatuslineLevel level) {
    lv_obj_t* dot = lv_obj_get_child(obj, DOT_INDEX);
    lv_obj_set_state(dot, WARNING_STATE, level == LVGL_STATUSLINE_LEVEL_WARNING);
    lv_obj_set_state(dot, ERROR_STATE, level == LVGL_STATUSLINE_LEVEL_ERROR);
}
