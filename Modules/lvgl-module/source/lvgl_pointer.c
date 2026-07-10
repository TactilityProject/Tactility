// SPDX-License-Identifier: Apache-2.0
#include <tactility/lvgl_pointer.h>

#include <tactility/drivers/pointer.h>

#include <stdlib.h>

struct LvglPointerCtx {
    struct Device* device;
};

// Bus reads are expected to complete quickly; bound the wait so a stalled controller can't block the LVGL indev poll.
static const TickType_t LVGL_POINTER_READ_TIMEOUT = pdMS_TO_TICKS(10);

static void lvgl_pointer_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    struct LvglPointerCtx* ctx = (struct LvglPointerCtx*)lv_indev_get_driver_data(indev);

    if (pointer_read_data(ctx->device, LVGL_POINTER_READ_TIMEOUT) != ERROR_NONE) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    uint16_t x = 0;
    uint16_t y = 0;
    uint8_t point_count = 0;
    bool touched = pointer_get_touched_points(ctx->device, &x, &y, NULL, &point_count, 1);

    if (touched && point_count > 0) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

error_t lvgl_pointer_add(struct Device* device, lv_display_t* display, lv_indev_t** out_indev) {
    if (device == NULL || out_indev == NULL) {
        return ERROR_INVALID_ARGUMENT;
    }
    if (device_get_type(device) != &POINTER_TYPE) {
        return ERROR_INVALID_ARGUMENT;
    }

    struct LvglPointerCtx* ctx = (struct LvglPointerCtx*)malloc(sizeof(struct LvglPointerCtx));
    if (ctx == NULL) {
        return ERROR_OUT_OF_MEMORY;
    }
    ctx->device = device;

    lv_indev_t* indev = lv_indev_create();
    if (indev == NULL) {
        free(ctx);
        return ERROR_OUT_OF_MEMORY;
    }

    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lvgl_pointer_read_cb);
    lv_indev_set_driver_data(indev, ctx);
    if (display != NULL) {
        lv_indev_set_display(indev, display);
    }

    *out_indev = indev;
    return ERROR_NONE;
}

void lvgl_pointer_remove(lv_indev_t* indev) {
    if (indev == NULL) {
        return;
    }

    struct LvglPointerCtx* ctx = (struct LvglPointerCtx*)lv_indev_get_driver_data(indev);
    lv_indev_delete(indev);
    free(ctx);
}
