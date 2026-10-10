// SPDX-License-Identifier: Apache-2.0
#define LV_USE_PRIVATE_API 1 // For the lv_obj_class_t declaration

#include <lvgl/widgets/focus_container.h>

// Not static: the theme styles focus containers by this class
extern "C" const lv_obj_class_t lvgl_focus_container_class = {
    .base_class = &lv_obj_class,
    .constructor_cb = nullptr,
    .destructor_cb = nullptr,
    .event_cb = nullptr,
    .user_data = nullptr,
    .name = "focus_container",
    .width_def = LV_SIZE_CONTENT,
    .height_def = LV_SIZE_CONTENT,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_TRUE,
    .instance_size = 0,
    .theme_inheritable = LV_OBJ_CLASS_THEME_INHERITABLE_FALSE
};

lv_obj_t* lvgl_focus_container_create(lv_obj_t* parent) {
    lv_obj_t* obj = lv_obj_class_create_obj(&lvgl_focus_container_class, parent);
    lv_obj_class_init_obj(obj);
    lv_obj_set_scrollable(obj, false);
    return obj;
}
