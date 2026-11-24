#define LV_USE_PRIVATE_API 1
#include <lvgl.h>

extern "C" {

bool lvgl_textarea_is_cursor_on_first_line(lv_obj_t* textarea) {
    const char* text = lv_textarea_get_text(textarea);
    if (text == nullptr) {
        return true;
    }

    uint32_t cursor = lv_textarea_get_cursor_pos(textarea);
    uint32_t char_index = 0;
    uint32_t byte_index = 0;

    while (text[byte_index] != '\0' && char_index < cursor) {
        if (text[byte_index] == '\n') {
            return false;
        }

        byte_index += lv_text_encoded_size(&text[byte_index]);
        char_index++;
    }

    return true;
}

bool lvgl_textarea_is_cursor_on_last_line(lv_obj_t* textarea) {
    const char* text = lv_textarea_get_text(textarea);
    if (text == nullptr) {
        return true;
    }

    uint32_t cursor = lv_textarea_get_cursor_pos(textarea);
    uint32_t char_index = 0;
    uint32_t byte_index = 0;

    // Advance to the cursor position.
    while (text[byte_index] != '\0' && char_index < cursor) {
        uint32_t size = lv_text_encoded_size(&text[byte_index]);
        byte_index += (size > 0) ? size : 1;
        char_index++;
    }

    // Check for newlines after the cursor.
    while (text[byte_index] != '\0') {
        if (text[byte_index] == '\n') {
            return false;
        }

        uint32_t size = lv_text_encoded_size(&text[byte_index]);
        byte_index += (size > 0) ? size : 1;
    }

    return true;
}

}
