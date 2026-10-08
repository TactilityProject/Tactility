// SPDX-License-Identifier: Apache-2.0
#include <lvgl/insets.h>
#include <lvgl/insets_math.h>
#include <lvgl/devices/device_context.h>

#include <algorithm>
#include <new>

static DisplayShape get_shape(lv_display_t* display) {
    DisplayShape shape = { .shape = DISPLAY_SHAPE_RECTANGLE, .corner_radius = 0 };
    const auto* wrapper = static_cast<const LvglDeviceContext*>(lv_display_get_driver_data(display));
    if (wrapper != nullptr && wrapper->device != nullptr) {
        display_get_shape(wrapper->device, &shape);
    }
    return shape;
}

struct EdgePadding {
    lv_obj_t* obj;
    // The padding before the display's edge was added
    int32_t base_left;
    int32_t base_right;
};

static void update_edge_padding(void* data) {
    const auto* padding = static_cast<const EdgePadding*>(data);
    lv_display_t* display = lv_obj_get_display(padding->obj);
    lv_area_t coords;
    lv_obj_get_coords(padding->obj, &coords);
    // The curve is widest at the object's edge that is nearest to the display's top or bottom
    const int32_t inset = std::max(lvgl_display_get_row_inset(display, coords.y1), lvgl_display_get_row_inset(display, coords.y2));
    const int32_t right_edge = lv_display_get_horizontal_resolution(display) - 1;
    const int32_t pad_left = padding->base_left + std::max<int32_t>(0, inset - coords.x1);
    const int32_t pad_right = padding->base_right + std::max<int32_t>(0, inset - (right_edge - coords.x2));
    // Setting a style marks the layout as dirty, which would resize the object and call this again
    if (lv_obj_get_style_pad_left(padding->obj, LV_PART_MAIN) != pad_left) {
        lv_obj_set_style_pad_left(padding->obj, pad_left, LV_STATE_DEFAULT);
    }
    if (lv_obj_get_style_pad_right(padding->obj, LV_PART_MAIN) != pad_right) {
        lv_obj_set_style_pad_right(padding->obj, pad_right, LV_STATE_DEFAULT);
    }
}

// The position is final once the layout pass that changed the size has finished
static void on_size_changed(lv_event_t* event) {
    lv_async_call_cancel(update_edge_padding, lv_event_get_user_data(event));
    lv_async_call(update_edge_padding, lv_event_get_user_data(event));
}

static void on_deleted(lv_event_t* event) {
    auto* padding = static_cast<EdgePadding*>(lv_event_get_user_data(event));
    lv_async_call_cancel(update_edge_padding, padding);
    delete padding;
}

extern "C" {

void lvgl_obj_add_edge_padding(lv_obj_t* obj) {
    if (get_shape(lv_obj_get_display(obj)).shape == DISPLAY_SHAPE_RECTANGLE) {
        return;
    }
    auto* padding = new(std::nothrow) EdgePadding {
        .obj = obj,
        .base_left = lv_obj_get_style_pad_left(obj, LV_PART_MAIN),
        .base_right = lv_obj_get_style_pad_right(obj, LV_PART_MAIN)
    };
    if (padding == nullptr) {
        return;
    }
    lv_obj_add_event_cb(obj, on_size_changed, LV_EVENT_SIZE_CHANGED, padding);
    lv_obj_add_event_cb(obj, on_deleted, LV_EVENT_DELETE, padding);
}

void lvgl_display_get_shape(lv_display_t* display, DisplayShape* out_shape) {
    *out_shape = get_shape(display);
}

void lvgl_display_get_insets(lv_display_t* display, LvglInsets* out_insets) {
    *out_insets = lvgl_insets_calculate(get_shape(display), lv_display_get_horizontal_resolution(display), lv_display_get_vertical_resolution(display));
}

int32_t lvgl_display_get_row_inset(lv_display_t* display, int32_t y) {
    return lvgl_insets_calculate_row(get_shape(display), lv_display_get_horizontal_resolution(display), lv_display_get_vertical_resolution(display), y);
}

}
