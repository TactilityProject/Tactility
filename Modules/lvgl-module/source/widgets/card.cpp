// SPDX-License-Identifier: Apache-2.0
#define LV_USE_PRIVATE_API 1 // For the lv_obj_class_t declaration

#include <lvgl/widgets/card.h>

// Not static: the theme styles cards by this class
extern "C" const lv_obj_class_t lvgl_card_class = {
    .base_class = &lv_obj_class,
    .constructor_cb = nullptr,
    .destructor_cb = nullptr,
    .event_cb = nullptr,
    .user_data = nullptr,
    .name = "card",
    .width_def = 0,
    .height_def = 0,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_INHERIT,
    .instance_size = 0,
    .theme_inheritable = LV_OBJ_CLASS_THEME_INHERITABLE_FALSE
};

lv_obj_t* lvgl_card_create(lv_obj_t* parent) {
    lv_obj_t* obj = lv_obj_class_create_obj(&lvgl_card_class, parent);
    lv_obj_class_init_obj(obj);
    return obj;
}
