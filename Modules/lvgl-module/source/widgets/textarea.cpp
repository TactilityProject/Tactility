#include <lvgl.h>

extern "C" {

bool lvgl_textarea_is_cursor_on_first_line(lv_obj_t* textarea) {
    const char* text = lv_textarea_get_text(textarea);
    if (text == nullptr) {
        return true;
    }

    uint32_t cursor = lv_textarea_get_cursor_pos(textarea);

    for (uint32_t i = 0; i < cursor; i++) {
        if (text[i] == '\n') {
            return false;
        }
    }

    return true;
}

bool lvgl_textarea_is_cursor_on_last_line(lv_obj_t* textarea) {
    const char* text = lv_textarea_get_text(textarea);
    if (text == nullptr) {
        return true;
    }

    uint32_t cursor = lv_textarea_get_cursor_pos(textarea);

    for (uint32_t i = cursor; text[i] != '\0'; i++) {
        if (text[i] == '\n') {
            return false;
        }
    }

    return true;
}

}
