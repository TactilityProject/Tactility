// SPDX-License-Identifier: Apache-2.0
#include <tactility/lvgl_pointer.h>

#include <tactility/drivers/pointer.h>

#include <stdlib.h>

#define TAG "lvgl_pointer"

struct LvglPointerCtx {
    struct Device* device;
    bool calibration_enabled;
    struct LvglPointerCalibration calibration;
};

// Bus reads are expected to complete quickly; bound the wait so a stalled controller can't block the LVGL indev poll.
static const TickType_t LVGL_POINTER_READ_TIMEOUT = pdMS_TO_TICKS(10);

// Tracks the first indev created by lvgl_pointer_add() still alive, for lvgl_pointer_get_default().
// Only ever set/cleared by lvgl_pointer_add()/lvgl_pointer_remove(), so it can never point at an
// indev created by other code (e.g. the deprecated HAL's own LVGL pointer registration).
static lv_indev_t* default_pointer_indev = NULL;

// Mirrors Tactility/Source/settings/TouchCalibrationSettings.cpp's isValid().
static const int32_t LVGL_POINTER_CALIBRATION_MIN_RANGE = 20;

static bool lvgl_pointer_calibration_is_valid(const struct LvglPointerCalibration* calibration) {
    return calibration->x_max > calibration->x_min &&
        calibration->y_max > calibration->y_min &&
        (calibration->x_max - calibration->x_min) >= LVGL_POINTER_CALIBRATION_MIN_RANGE &&
        (calibration->y_max - calibration->y_min) >= LVGL_POINTER_CALIBRATION_MIN_RANGE;
}

// Linear per-axis rescale of [x_min,x_max]/[y_min,y_max] onto [0,target_x_max]/[0,target_y_max],
// clamped. Mirrors TouchCalibrationSettings.cpp's applyCalibration(). Kept as a standalone
// function (not inlined into the read callback) so the math is isolated and easy to reason about.
static void lvgl_pointer_calibration_apply(
    const struct LvglPointerCalibration* calibration,
    int32_t target_x_max,
    int32_t target_y_max,
    uint16_t* x,
    uint16_t* y
) {
    int64_t mapped_x = ((int64_t)*x - calibration->x_min) * target_x_max /
        ((int64_t)calibration->x_max - calibration->x_min);
    int64_t mapped_y = ((int64_t)*y - calibration->y_min) * target_y_max /
        ((int64_t)calibration->y_max - calibration->y_min);

    if (mapped_x < 0) mapped_x = 0;
    if (mapped_x > target_x_max) mapped_x = target_x_max;
    if (mapped_y < 0) mapped_y = 0;
    if (mapped_y > target_y_max) mapped_y = target_y_max;

    *x = (uint16_t)mapped_x;
    *y = (uint16_t)mapped_y;
}

// Reads the touch controller and applies calibration entirely in the graphics driver's own
// native (LV_DISPLAY_ROTATION_0) coordinate space - native_x_max/native_y_max are just the panel's
// fixed pixel dimensions, not a rotation. This function has no notion of LVGL rotation at all:
// calibration corrects the raw sensor's fixed physical mapping, which never changes with on-screen
// orientation, so it doesn't belong anywhere near rotation math.
static bool lvgl_pointer_read_calibrated(struct LvglPointerCtx* ctx, int32_t native_x_max, int32_t native_y_max, uint16_t* x, uint16_t* y) {
    if (pointer_read_data(ctx->device, LVGL_POINTER_READ_TIMEOUT) != ERROR_NONE) {
        return false;
    }

    uint8_t point_count = 0;
    if (!pointer_get_touched_points(ctx->device, x, y, NULL, &point_count, 1) || point_count == 0) {
        return false;
    }

    if (ctx->calibration_enabled && native_x_max > 0 && native_y_max > 0) {
        lvgl_pointer_calibration_apply(&ctx->calibration, native_x_max, native_y_max, x, y);
    }

    return true;
}

// The actual LVGL indev read callback: wraps lvgl_pointer_read_calibrated() and, only here, applies
// the rotation needed to place the (still native-space) point into the currently active LVGL
// logical space - unconditionally, since native-space coordinates always need this regardless of
// whether calibration is enabled.
static void lvgl_pointer_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    struct LvglPointerCtx* ctx = (struct LvglPointerCtx*)lv_indev_get_driver_data(indev);
    lv_display_t* display = lv_indev_get_display(indev);

    // lv_display_get_original_*_resolution() is the native (LV_DISPLAY_ROTATION_0) size,
    // unaffected by the display's current rotation - no rotation lookup needed to get it.
    int32_t native_x_max = display != NULL ? lv_display_get_original_horizontal_resolution(display) - 1 : 0;
    int32_t native_y_max = display != NULL ? lv_display_get_original_vertical_resolution(display) - 1 : 0;

    uint16_t x = 0;
    uint16_t y = 0;
    if (!lvgl_pointer_read_calibrated(ctx, native_x_max, native_y_max, &x, &y)) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
}

error_t lvgl_pointer_add(struct Device* device, lv_display_t* display, lv_indev_t** out_indev) {
    if (device == NULL || out_indev == NULL) {
        return ERROR_INVALID_ARGUMENT;
    }
    if (device_get_type(device) != &POINTER_TYPE) {
        return ERROR_INVALID_ARGUMENT;
    }

    struct LvglPointerCtx* ctx = calloc(1, sizeof(struct LvglPointerCtx));
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

    if (default_pointer_indev == NULL) {
        default_pointer_indev = indev;
    }

    *out_indev = indev;
    return ERROR_NONE;
}

lv_indev_t* lvgl_pointer_get_default(void) {
    return default_pointer_indev;
}

error_t lvgl_pointer_set_calibration(lv_indev_t* indev, const struct LvglPointerCalibration* calibration) {
    if (indev == NULL) {
        return ERROR_INVALID_ARGUMENT;
    }
    struct LvglPointerCtx* ctx = lv_indev_get_driver_data(indev);

    if (calibration == NULL) {
        ctx->calibration_enabled = false;
        return ERROR_NONE;
    }
    if (!lvgl_pointer_calibration_is_valid(calibration)) {
        return ERROR_INVALID_ARGUMENT;
    }

    ctx->calibration = *calibration;
    ctx->calibration_enabled = true;
    return ERROR_NONE;
}

bool lvgl_pointer_get_calibration(lv_indev_t* indev, struct LvglPointerCalibration* out_calibration) {
    if (indev == NULL || out_calibration == NULL) {
        return false;
    }
    struct LvglPointerCtx* ctx = lv_indev_get_driver_data(indev);
    if (!ctx->calibration_enabled) {
        return false;
    }
    *out_calibration = ctx->calibration;
    return true;
}

void lvgl_pointer_remove(lv_indev_t* indev) {
    if (indev == NULL) {
        return;
    }

    struct LvglPointerCtx* ctx = lv_indev_get_driver_data(indev);
    if (default_pointer_indev == indev) {
        default_pointer_indev = NULL;
    }
    lv_indev_delete(indev);
    free(ctx);
}
