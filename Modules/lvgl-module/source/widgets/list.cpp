// SPDX-License-Identifier: Apache-2.0
#define LV_USE_PRIVATE_API 1 // For the lv_obj_class_t declaration

#include <lvgl/widgets/list.h>

// Not static: the theme styles lists by these classes
extern "C" const lv_obj_class_t lvgl_list_class = {
    .base_class = &lv_obj_class,
    .constructor_cb = nullptr,
    .destructor_cb = nullptr,
    .event_cb = nullptr,
    .user_data = nullptr,
    .name = "list",
    .width_def = (LV_DPI_DEF * 3) / 2,
    .height_def = LV_DPI_DEF * 2,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_INHERIT,
    .instance_size = 0,
    .theme_inheritable = LV_OBJ_CLASS_THEME_INHERITABLE_FALSE
};

extern "C" const lv_obj_class_t lvgl_list_text_class = {
    .base_class = &lv_label_class,
    .constructor_cb = nullptr,
    .destructor_cb = nullptr,
    .event_cb = nullptr,
    .user_data = nullptr,
    .name = "list_text",
    .width_def = LV_PCT(100),
    .height_def = LV_SIZE_CONTENT,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_INHERIT,
    .instance_size = 0,
    .theme_inheritable = LV_OBJ_CLASS_THEME_INHERITABLE_FALSE
};

extern "C" const lv_obj_class_t lvgl_list_button_class = {
    .base_class = &lv_button_class,
    .constructor_cb = nullptr,
    .destructor_cb = nullptr,
    .event_cb = nullptr,
    .user_data = nullptr,
    .name = "list_button",
    .width_def = LV_PCT(100),
    .height_def = LV_SIZE_CONTENT,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_INHERIT,
    .instance_size = 0,
    .theme_inheritable = LV_OBJ_CLASS_THEME_INHERITABLE_FALSE
};

lv_obj_t* lvgl_list_create(lv_obj_t* parent) {
    lv_obj_t* obj = lv_obj_class_create_obj(&lvgl_list_class, parent);
    lv_obj_class_init_obj(obj);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    return obj;
}

lv_obj_t* lvgl_list_add_text(lv_obj_t* list, const char* text) {
    lv_obj_t* obj = lv_obj_class_create_obj(&lvgl_list_text_class, list);
    lv_obj_class_init_obj(obj);
    lv_label_set_text(obj, text);
    return obj;
}

lv_obj_t* lvgl_list_add_button(lv_obj_t* list, const void* icon, const char* text) {
    lv_obj_t* obj = lv_obj_class_create_obj(&lvgl_list_button_class, list);
    lv_obj_class_init_obj(obj);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);

    if (icon != nullptr) {
        lv_obj_t* image = lv_image_create(obj);
        lv_image_set_src(image, icon);
    }

    if (text != nullptr) {
        lv_obj_t* label = lv_label_create(obj);
        lv_label_set_text(label, text);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        lv_obj_set_flex_grow(label, 1);
    }

    return obj;
}
