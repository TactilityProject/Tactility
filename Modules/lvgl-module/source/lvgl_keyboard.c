// SPDX-License-Identifier: Apache-2.0
#include <tactility/lvgl_keyboard.h>

#include <tactility/drivers/keyboard.h>

#include <stdlib.h>

struct LvglKeyboardCtx {
    struct Device* device;
};

static void lvgl_keyboard_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    struct LvglKeyboardCtx* ctx = (struct LvglKeyboardCtx*)lv_indev_get_driver_data(indev);

    struct KeyboardKeyData key_data = {0};
    if (keyboard_read_key(ctx->device, &key_data) != ERROR_NONE) {
        data->state = LV_INDEV_STATE_RELEASED;
        data->continue_reading = false;
        return;
    }

    // KeyboardKeyData deliberately mirrors lv_indev_data_t's key/continue_reading fields, so no translation is needed.
    data->key = key_data.key;
    data->state = key_data.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->continue_reading = key_data.continue_reading;
}

error_t lvgl_keyboard_add(struct Device* device, lv_display_t* display, lv_indev_t** out_indev) {
    if (device == NULL || out_indev == NULL) {
        return ERROR_INVALID_ARGUMENT;
    }
    if (device_get_type(device) != &KEYBOARD_TYPE) {
        return ERROR_INVALID_ARGUMENT;
    }

    struct LvglKeyboardCtx* ctx = (struct LvglKeyboardCtx*)malloc(sizeof(struct LvglKeyboardCtx));
    if (ctx == NULL) {
        return ERROR_OUT_OF_MEMORY;
    }
    ctx->device = device;

    lv_indev_t* indev = lv_indev_create();
    if (indev == NULL) {
        free(ctx);
        return ERROR_OUT_OF_MEMORY;
    }

    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, lvgl_keyboard_read_cb);
    lv_indev_set_driver_data(indev, ctx);
    if (display != NULL) {
        lv_indev_set_display(indev, display);
    }

    *out_indev = indev;
    return ERROR_NONE;
}

void lvgl_keyboard_remove(lv_indev_t* indev) {
    if (indev == NULL) {
        return;
    }

    struct LvglKeyboardCtx* ctx = (struct LvglKeyboardCtx*)lv_indev_get_driver_data(indev);
    lv_indev_delete(indev);
    free(ctx);
}
