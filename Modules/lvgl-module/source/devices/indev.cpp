#include <lvgl/devices/indev.h>

extern "C" {

bool lvgl_indev_exists(lv_indev_type_t type) {
    for (lv_indev_t* indev = lv_indev_get_next(nullptr); indev != nullptr; indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_type(indev) == type) {
            return true;
        }
    }
    return false;
}

} // extern "C"
