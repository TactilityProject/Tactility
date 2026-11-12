// SPDX-License-Identifier: Apache-2.0
#include <lvgl/devices/mouse.h>
#include <lvgl/devices/device_context.h>
#include <lvgl/ui_assets.h>

#include <tactility/drivers/mouse.h>

#include <new>

struct LvglMouseCtx {
    // Position in the coordinate space of the display's current rotation
    int32_t x = 0;
    int32_t y = 0;
    lv_obj_t* cursor = nullptr;
};

static inline int32_t clamp(int32_t value, int32_t min_value, int32_t max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static void lvgl_mouse_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* wrapper = static_cast<LvglDeviceContext*>(lv_indev_get_driver_data(indev));
    auto* ctx = static_cast<LvglMouseCtx*>(wrapper->context);

    int32_t dx = 0;
    int32_t dy = 0;
    mouse_read_delta(wrapper->device, &dx, &dy);
    bool pressed = false;
    mouse_get_button_pressed(wrapper->device, 0, &pressed);

    lv_display_t* display = lv_indev_get_display(indev);
    if (display == nullptr) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    int32_t width = lv_display_get_horizontal_resolution(display);
    int32_t height = lv_display_get_vertical_resolution(display);
    ctx->x = clamp(ctx->x + dx, 0, width - 1);
    ctx->y = clamp(ctx->y + dy, 0, height - 1);

    int32_t original_width = lv_display_get_original_horizontal_resolution(display);
    int32_t original_height = lv_display_get_original_vertical_resolution(display);
    switch (lv_display_get_rotation(display)) {
        case LV_DISPLAY_ROTATION_90:
            data->point.x = static_cast<lv_coord_t>(ctx->y);
            data->point.y = static_cast<lv_coord_t>(original_height - ctx->x - 1);
            break;
        case LV_DISPLAY_ROTATION_180:
            data->point.x = static_cast<lv_coord_t>(original_width - ctx->x - 1);
            data->point.y = static_cast<lv_coord_t>(original_height - ctx->y - 1);
            break;
        case LV_DISPLAY_ROTATION_270:
            data->point.x = static_cast<lv_coord_t>(original_width - ctx->y - 1);
            data->point.y = static_cast<lv_coord_t>(ctx->x);
            break;
        default:
            data->point.x = static_cast<lv_coord_t>(ctx->x);
            data->point.y = static_cast<lv_coord_t>(ctx->y);
            break;
    }
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;

    if (dx != 0 || dy != 0 || pressed) {
        lv_display_trigger_activity(display);
    }
}

extern "C" {

error_t lvgl_mouse_add(struct Device* device, lv_display_t* display, lv_indev_t** out_indev) {
    if (device == nullptr || out_indev == nullptr) {
        return ERROR_INVALID_ARGUMENT;
    }
    if (device_get_type(device) != &MOUSE_TYPE) {
        return ERROR_INVALID_ARGUMENT;
    }

    lv_indev_t* existing = lvgl_mouse_find_by_device(device);
    if (existing != nullptr) {
        *out_indev = existing;
        return ERROR_NONE;
    }

    auto* ctx = new(std::nothrow) LvglMouseCtx();
    if (ctx == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    auto* wrapper = new(std::nothrow) LvglDeviceContext(ctx);
    if (wrapper == nullptr) {
        delete ctx;
        return ERROR_OUT_OF_MEMORY;
    }
    wrapper->device = device;

    lv_indev_t* indev = lv_indev_create();
    if (indev == nullptr) {
        delete wrapper;
        return ERROR_OUT_OF_MEMORY;
    }

    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lvgl_mouse_read_cb);
    lv_indev_set_driver_data(indev, wrapper);
    if (display != nullptr) {
        lv_indev_set_display(indev, display);
        ctx->x = lv_display_get_horizontal_resolution(display) / 2;
        ctx->y = lv_display_get_vertical_resolution(display) / 2;
    }

    ctx->cursor = lv_image_create(lv_layer_sys());
    if (ctx->cursor != nullptr) {
        lv_obj_set_clickable(ctx->cursor, false);
        lv_image_set_src(ctx->cursor, lvgl_get_cursor_image());
        lv_indev_set_cursor(indev, ctx->cursor);
    }

    *out_indev = indev;
    return ERROR_NONE;
}

void lvgl_mouse_remove(lv_indev_t* indev) {
    if (indev == nullptr) {
        return;
    }

    auto* wrapper = static_cast<LvglDeviceContext*>(lv_indev_get_driver_data(indev));
    auto* ctx = static_cast<LvglMouseCtx*>(wrapper->context);
    if (ctx->cursor != nullptr) {
        lv_obj_delete(ctx->cursor);
    }
    lv_indev_delete(indev);
    delete wrapper;
}

lv_indev_t* lvgl_mouse_find_by_device(struct Device* device) {
    for (lv_indev_t* indev = lv_indev_get_next(nullptr); indev != nullptr; indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER || lv_indev_get_read_cb(indev) != lvgl_mouse_read_cb) {
            continue;
        }
        auto* wrapper = static_cast<LvglDeviceContext*>(lv_indev_get_driver_data(indev));
        if (wrapper != nullptr && wrapper->device == device) {
            return indev;
        }
    }
    return nullptr;
}

} // extern "C"
