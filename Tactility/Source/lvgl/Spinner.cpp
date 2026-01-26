#define LV_USE_PRIVATE_API 1 // For actual lv_obj_t declaration

#include <Tactility/Assets.h>
#include <Tactility/CoreDefines.h>

#include <lvgl.h>

namespace tt::lvgl {

static void spinner_constructor(const lv_obj_class_t* object_class, lv_obj_t* object);

const lv_obj_class_t tt_spinner_class = {
    .base_class = &lv_image_class,
    .constructor_cb = spinner_constructor,
    .destructor_cb = nullptr,
    .event_cb = nullptr,
    .user_data = nullptr,
    .name = "tt_spinner",
    .width_def = 0,
    .height_def = 0,
    .editable = 0,
    .group_def = 0,
    .instance_size = 0,
    .theme_inheritable = 0
};

lv_obj_t* spinner_create(lv_obj_t* parent) {
    lv_obj_t* obj = lv_obj_class_create_obj(&tt_spinner_class, parent);
    lv_obj_class_init_obj(obj);

    lv_image_set_src(obj, TT_ASSETS_UI_SPINNER);

    return obj;
}

static void anim_rotation_callback(void* var, int32_t v) {
    auto* object = (lv_obj_t*) var;
    auto width = lv_obj_get_width(object);
    auto height = lv_obj_get_width(object);
    lv_obj_set_style_transform_pivot_x(object, width / 2, 0);
    lv_obj_set_style_transform_pivot_y(object, height / 2, 0);
    lv_obj_set_style_transform_rotation(object, v, 0);
}

static void spinner_constructor(const lv_obj_class_t* object_class, lv_obj_t* object) {
    lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, object);
    lv_anim_set_values(&a, 0, 3600);
    lv_anim_set_duration(&a, 800);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, anim_rotation_callback);
    lv_anim_start(&a);
}

}
