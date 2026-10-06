// SPDX-License-Identifier: Apache-2.0
#define LV_USE_PRIVATE_API 1 // For the lv_obj_class_t declaration

#include <lvgl/widgets/icon_button.h>

// Not static: the theme styles icon buttons by these classes. Each variant is a separate class,
// because the theme applies its styles while the object is created.

extern "C" const lv_obj_class_t lvgl_icon_button_class = {
    .base_class = &lv_button_class,
    .constructor_cb = nullptr,
    .destructor_cb = nullptr,
    .event_cb = nullptr,
    .user_data = nullptr,
    .name = "icon_button",
    .width_def = 0,
    .height_def = 0,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_INHERIT,
    .instance_size = 0,
    .theme_inheritable = LV_OBJ_CLASS_THEME_INHERITABLE_FALSE
};

extern "C" const lv_obj_class_t lvgl_icon_button_filled_class = {
    .base_class = &lv_button_class,
    .constructor_cb = nullptr,
    .destructor_cb = nullptr,
    .event_cb = nullptr,
    .user_data = nullptr,
    .name = "icon_button_filled",
    .width_def = 0,
    .height_def = 0,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_INHERIT,
    .instance_size = 0,
    .theme_inheritable = LV_OBJ_CLASS_THEME_INHERITABLE_FALSE
};

extern "C" const lv_obj_class_t lvgl_icon_button_tonal_class = {
    .base_class = &lv_button_class,
    .constructor_cb = nullptr,
    .destructor_cb = nullptr,
    .event_cb = nullptr,
    .user_data = nullptr,
    .name = "icon_button_tonal",
    .width_def = 0,
    .height_def = 0,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_INHERIT,
    .instance_size = 0,
    .theme_inheritable = LV_OBJ_CLASS_THEME_INHERITABLE_FALSE
};

static const lv_obj_class_t* get_variant_class(enum LvglIconButtonVariant variant) {
    switch (variant) {
        case LVGL_ICON_BUTTON_FILLED:
            return &lvgl_icon_button_filled_class;
        case LVGL_ICON_BUTTON_TONAL:
            return &lvgl_icon_button_tonal_class;
        default:
            return &lvgl_icon_button_class;
    }
}

lv_obj_t* lvgl_icon_button_create_variant(lv_obj_t* parent, enum LvglIconButtonVariant variant) {
    lv_obj_t* obj = lv_obj_class_create_obj(get_variant_class(variant), parent);
    lv_obj_class_init_obj(obj);
    return obj;
}

lv_obj_t* lvgl_icon_button_create(lv_obj_t* parent) {
    return lvgl_icon_button_create_variant(parent, LVGL_ICON_BUTTON_STANDARD);
}
