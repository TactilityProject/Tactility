#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <lvgl.h>

#include <stdint.h>

struct KeyboardKeyData;

void lvgl_keyboard_on_start_lvgl();
void lvgl_keyboard_on_stop_lvgl();

/**
 * @brief Translates a key codepoint into the LVGL key for the object that the indev's group focuses.
 * The arrow up and down keys move the focus to the previous and next widget, or within a grid navigation container.
 * @param[in] indev the indev that reports the key
 * @param[in] key_data the raw keyboard event data
 * @return the LVGL key
 */
uint32_t lvgl_keyboard_translate_key(lv_indev_t* indev, const struct KeyboardKeyData* key_data);

#ifdef __cplusplus
}
#endif
