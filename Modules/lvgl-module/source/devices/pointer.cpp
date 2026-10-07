// SPDX-License-Identifier: Apache-2.0
#include <lvgl/devices/pointer.h>
#include <lvgl/devices/device_context.h>
#include <lvgl/grid_navigation.h>

#include <tactility/device.h>
#include <tactility/drivers/pointer.h>

#include <cstring>
#include <new>

constexpr auto* TAG = "lvgl_pointer";

// Bus reads are expected to complete quickly; bound the wait so a stalled controller can't block the LVGL indev poll.
static const TickType_t LVGL_POINTER_READ_TIMEOUT = pdMS_TO_TICKS(10);

// Tracks the first indev created by lvgl_pointer_add() still alive, for lvgl_pointer_get_default().
// Only ever set/cleared by lvgl_pointer_add()/lvgl_pointer_remove(), so it can never point at an
// indev created by other code (e.g. the deprecated HAL's own LVGL pointer registration).
static lv_indev_t* default_pointer_indev = NULL;

// Mirrors Tactility/Source/settings/TouchCalibrationSettings.cpp's isValid().
static const int32_t LVGL_POINTER_CALIBRATION_MIN_RANGE = 20;

// Caps nearest-neighbor slot tracking (lvgl_pointer_pool_assign) to a fraction of screen width,
// so a lifted finger's slot doesn't jump to grab an unrelated new touch elsewhere on screen.
// Scaled by resolution, not a flat pixel value, so it stays proportionate on any display size.
// Set generously \wide: a too-tight cap misreads a fast scroll's own motion as release+re-press,
// firing whatever is under the finger mid-drag.
//
// \wide False positives (treating one continued drag as two separate touches) are far more
//     disruptive than false negatives (merging an unrelated same-spot lift+relanding), which
//     favors erring toward a larger cap.
static const int32_t LVGL_POINTER_MAX_TRACK_DIST_FRACTION = 3; // 1/3 of screen width

// One physical touch device backs LVGL_POINTER_MAX_SLOTS independent lv_indev_t instances (a
// "pool"), so LVGL can track that many simultaneous fingers - LVGL v9 has no multi-point indev
// concept (confirmed against lv_indev.h/lv_indev.c: lv_indev_data_t carries exactly one
// lv_point_t/state pair), so N simultaneous independently-clickable widgets requires N indevs.
// Each slot's read callback shares one raw multi-touch read per round rather than each slot
// hitting the bus independently - see lvgl_pointer_read_cb().
struct LvglPointerPool {
    struct Device* device;
    bool calibration_enabled;
    struct LvglPointerCalibration calibration;

    uint8_t slot_count;
    lv_indev_t* slot_indev[LVGL_POINTER_MAX_SLOTS];

    // Per-slot "which finger" tracking. A slot with active=false reports RELEASED and is up for
    // grabs by any unmatched raw point next round.
    bool slot_active[LVGL_POINTER_MAX_SLOTS];
    lv_point_t slot_point[LVGL_POINTER_MAX_SLOTS];

    // Shared raw-read cache for this round. Refreshed by whichever slot's read callback runs
    // first each round (see round_pos); the rest just consume it - one bus transaction per
    // slot_count read callbacks, not one per slot.
    uint8_t round_pos;
    uint16_t raw_x[LVGL_POINTER_MAX_SLOTS];
    uint16_t raw_y[LVGL_POINTER_MAX_SLOTS];
    uint8_t raw_count;
};

// Safely narrows an arbitrary lv_indev_t's driver data down to this module's LvglPointerPool,
// or NULL if indev is NULL, wasn't created by lvgl_pointer_add() (LvglDeviceContext is shared by
// every LVGL indev/display this module creates - pointer, trackball, keyboard - so driver_data
// alone doesn't prove it's a pointer pool), or is mid-teardown (context nulled by
// lvgl_pointer_remove()/lvgl_pointer_add()'s cleanup path before the wrapper itself is freed).
static struct LvglPointerPool* lvgl_pointer_pool_from_indev(lv_indev_t* indev) {
    if (indev == NULL) {
        return NULL;
    }
    auto* wrapper = (struct LvglDeviceContext*)lv_indev_get_driver_data(indev);
    if (wrapper == NULL || wrapper->device == NULL || device_get_type(wrapper->device) != &POINTER_TYPE) {
        return NULL;
    }
    return (struct LvglPointerPool*)wrapper->context;
}

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

// Reads all currently-touched points from the device into pool->raw_x/raw_y/raw_count, applying
// calibration in the graphics driver's own native (LV_DISPLAY_ROTATION_0) coordinate space -
// native_x_max/native_y_max are just the panel's fixed pixel dimensions, not a rotation. This
// function has no notion of LVGL rotation at all: calibration corrects the raw sensor's fixed
// physical mapping, which never changes with on-screen orientation, so it doesn't belong anywhere
// near rotation math.
static void lvgl_pointer_pool_refresh(struct LvglPointerPool* pool, int32_t native_x_max, int32_t native_y_max) {
    pool->raw_count = 0;

    if (pointer_read_data(pool->device, LVGL_POINTER_READ_TIMEOUT) != ERROR_NONE) {
        return;
    }

    uint8_t point_count = 0;
    if (!pointer_get_touched_points(pool->device, pool->raw_x, pool->raw_y, NULL, &point_count, LVGL_POINTER_MAX_SLOTS) || point_count == 0) {
        return;
    }
    if (point_count > LVGL_POINTER_MAX_SLOTS) point_count = LVGL_POINTER_MAX_SLOTS;

    if (pool->calibration_enabled && native_x_max > 0 && native_y_max > 0) {
        for (uint8_t i = 0; i < point_count; i++) {
            lvgl_pointer_calibration_apply(&pool->calibration, native_x_max, native_y_max, &pool->raw_x[i], &pool->raw_y[i]);
        }
    }

    pool->raw_count = point_count;
}

// Matches this round's raw points onto pool slots by nearest-neighbor to each slot's last known
// position, so a slot "follows" the same physical finger across rounds instead of jumping when
// the touch controller reports points in a different order (no touch-ID/tracking field exists
// anywhere in this stack - see esp_lcd_touch_get_coordinates()/PointerApi.get_touched_points()).
// Unmatched raw points (new touches) claim the nearest inactive slot. Slots with no matching
// point this round go inactive (RELEASED). Returns true when a new touch started.
static bool lvgl_pointer_pool_assign(struct LvglPointerPool* pool, int32_t native_x_max) {
    // Touch drivers clamp raw coordinates to the panel's configured native resolution regardless
    // of calibration, so native_x_max is a valid scale reference for the distance cap even when calibration is disabled. 
    // Falls back to a conservative fixed pixel value if the display/resolution isn't available for some reason.
    const int32_t max_track_dist = native_x_max > 0 ? (native_x_max / LVGL_POINTER_MAX_TRACK_DIST_FRACTION) : 150;
    const int32_t max_track_dist_sq = max_track_dist * max_track_dist;

    bool raw_claimed[LVGL_POINTER_MAX_SLOTS] = {};
    // A slot released this round must report RELEASED for at least one round before it can host
    // a new touch - otherwise pass 2 immediately reassigns it, and LVGL sees a jump instead of a
    // release-then-press.
    bool slot_released_now[LVGL_POINTER_MAX_SLOTS] = {};

    // First pass: let already-active slots keep following their nearest raw point, so a held
    // finger doesn't get reshuffled onto a different slot just because another finger moved.
    for (uint8_t s = 0; s < pool->slot_count; s++) {
        if (!pool->slot_active[s]) continue;
        int32_t best_dist = -1;
        int8_t best_raw = -1;
        for (uint8_t r = 0; r < pool->raw_count; r++) {
            if (raw_claimed[r]) continue;
            int32_t dx = (int32_t)pool->raw_x[r] - pool->slot_point[s].x;
            int32_t dy = (int32_t)pool->raw_y[r] - pool->slot_point[s].y;
            int32_t dist = dx * dx + dy * dy;
            if (best_raw < 0 || dist < best_dist) {
                best_dist = dist;
                best_raw = (int8_t)r;
            }
        }
        if (best_raw >= 0 && best_dist <= max_track_dist_sq) {
            raw_claimed[best_raw] = true;
            pool->slot_point[s].x = (lv_coord_t)pool->raw_x[best_raw];
            pool->slot_point[s].y = (lv_coord_t)pool->raw_y[best_raw];
        } else {
            pool->slot_active[s] = false;
            slot_released_now[s] = true;
        }
    }

    // Second pass: any unclaimed raw point is a new touch - hand it to the first inactive slot
    // that wasn't just released this round.
    bool touch_started = false;
    for (uint8_t r = 0; r < pool->raw_count; r++) {
        if (raw_claimed[r]) continue;
        for (uint8_t s = 0; s < pool->slot_count; s++) {
            if (pool->slot_active[s] || slot_released_now[s]) continue;
            pool->slot_active[s] = true;
            pool->slot_point[s].x = (lv_coord_t)pool->raw_x[r];
            pool->slot_point[s].y = (lv_coord_t)pool->raw_y[r];
            raw_claimed[r] = true;
            touch_started = true;
            break;
        }
    }
    return touch_started;
}

// The actual LVGL indev read callback, shared by every slot in the pool. Only the first slot to
// be read each round (round_pos wraps 0..slot_count-1) triggers the real bus read + reassignment;
// the rest just report whatever lvgl_pointer_pool_assign() decided for their slot. Which slot
// happens to run first varies (LVGL calls each indev's timer independently), but since all slots
// share one timer period they complete one full round every slot_count calls regardless of order.
static void lvgl_pointer_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    struct LvglPointerPool* pool = lvgl_pointer_pool_from_indev(indev);
    if (pool == NULL) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    uint8_t slot = 0;
    for (; slot < pool->slot_count; slot++) {
        if (pool->slot_indev[slot] == indev) break;
    }

    if (pool->round_pos == 0) {
        lv_display_t* display = lv_indev_get_display(indev);
        // lv_display_get_original_*_resolution() is the native (LV_DISPLAY_ROTATION_0) size,
        // unaffected by the display's current rotation - no rotation lookup needed to get it.
        int32_t native_x_max = display != NULL ? lv_display_get_original_horizontal_resolution(display) - 1 : 0;
        int32_t native_y_max = display != NULL ? lv_display_get_original_vertical_resolution(display) - 1 : 0;
        lvgl_pointer_pool_refresh(pool, native_x_max, native_y_max);
        // A touch hides the selection that keys made, so it doesn't look like two widgets are selected.
        // This runs before LVGL handles the press, so a widget that the touch focuses isn't affected.
        if (lvgl_pointer_pool_assign(pool, native_x_max) && lv_group_get_default() != NULL) {
            lvgl_focus_hide_key_selection(lv_group_get_default());
        }
    }
    pool->round_pos = (uint8_t)((pool->round_pos + 1) % pool->slot_count);

    if (slot < pool->slot_count && pool->slot_active[slot]) {
        data->point = pool->slot_point[slot];
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

error_t lvgl_pointer_add(struct Device* device, lv_display_t* display, uint8_t max_touch_points, lv_indev_t** out_indevs) {
    if (device == NULL || out_indevs == NULL) {
        return ERROR_INVALID_ARGUMENT;
    }
    if (device_get_type(device) != &POINTER_TYPE) {
        return ERROR_INVALID_ARGUMENT;
    }
    if (max_touch_points == 0) max_touch_points = 1;
    if (max_touch_points > LVGL_POINTER_MAX_SLOTS) max_touch_points = LVGL_POINTER_MAX_SLOTS;

    auto* pool = new(std::nothrow) LvglPointerPool();
    if (pool == NULL) {
        return ERROR_OUT_OF_MEMORY;
    }
    pool->device = device;
    pool->slot_count = max_touch_points;

    // Every slot gets its own LvglDeviceContext wrapper, but all of them point at this same pool.
    // On the way out - success or failure - every wrapper's context pointer is nulled before any
    // of them are deleted, then the pool itself is freed exactly once: LvglDeviceContext's
    // destructor does `::operator delete(context)`, so leaving more than one wrapper owning the
    // same pool pointer would double-free it (see lvgl_pointer_remove(), same pattern).
    uint8_t created = 0;
    for (; created < max_touch_points; created++) {
        auto* wrapper = new(std::nothrow) LvglDeviceContext(pool);
        if (wrapper == NULL) {
            break;
        }
        wrapper->device = device;

        lv_indev_t* indev = lv_indev_create();
        if (indev == NULL) {
            delete wrapper;
            break;
        }

        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, lvgl_pointer_read_cb);
        lv_indev_set_driver_data(indev, wrapper);
        if (display != NULL) {
            lv_indev_set_display(indev, display);
        }

        pool->slot_indev[created] = indev;
        out_indevs[created] = indev;
    }

    if (created < max_touch_points) {
        for (uint8_t j = 0; j < created; j++) {
            lv_indev_t* slot_indev = pool->slot_indev[j];
            auto* slot_wrapper = (LvglDeviceContext*)lv_indev_get_driver_data(slot_indev);
            slot_wrapper->context = NULL;
            lv_indev_delete(slot_indev);
            delete slot_wrapper;
        }
        delete pool;
        return ERROR_OUT_OF_MEMORY;
    }

    if (default_pointer_indev == NULL) {
        default_pointer_indev = pool->slot_indev[0];
    }

    return ERROR_NONE;
}

lv_indev_t* lvgl_pointer_get_default(void) {
    return default_pointer_indev;
}

// Any slot indev's wrapper->context points at the same shared pool, so calibration set through
// any one of them applies to the whole physical device/all its slots (one panel, one calibration).
error_t lvgl_pointer_set_calibration(lv_indev_t* indev, const struct LvglPointerCalibration* calibration) {
    struct LvglPointerPool* pool = lvgl_pointer_pool_from_indev(indev);
    if (pool == NULL) {
        return ERROR_INVALID_ARGUMENT;
    }

    if (calibration == NULL) {
        pool->calibration_enabled = false;
        return ERROR_NONE;
    }
    if (!lvgl_pointer_calibration_is_valid(calibration)) {
        return ERROR_INVALID_ARGUMENT;
    }

    pool->calibration = *calibration;
    pool->calibration_enabled = true;
    return ERROR_NONE;
}

bool lvgl_pointer_get_calibration(lv_indev_t* indev, struct LvglPointerCalibration* out_calibration) {
    if (out_calibration == NULL) {
        return false;
    }
    struct LvglPointerPool* pool = lvgl_pointer_pool_from_indev(indev);
    if (pool == NULL || !pool->calibration_enabled) {
        return false;
    }
    *out_calibration = pool->calibration;
    return true;
}

int8_t lvgl_pointer_get_slot_index(lv_indev_t* indev) {
    struct LvglPointerPool* pool = lvgl_pointer_pool_from_indev(indev);
    if (pool == NULL) {
        return -1;
    }
    for (uint8_t i = 0; i < pool->slot_count; i++) {
        if (pool->slot_indev[i] == indev) return (int8_t)i;
    }
    return -1;
}

// Removes every slot indev belonging to the same pool as `indev` (a partial pool removal isn't a
// case that comes up: apps ask for "the pointer device" and get every slot back from
// lvgl_pointer_add(), so they hold either all of a pool's indevs or none).
void lvgl_pointer_remove(lv_indev_t* indev) {
    if (indev == NULL) {
        return;
    }

    struct LvglDeviceContext* wrapper = (struct LvglDeviceContext*)lv_indev_get_driver_data(indev);
    struct LvglPointerPool* pool = (struct LvglPointerPool*)wrapper->context;
    uint8_t slot_count = pool->slot_count;
    lv_indev_t* slot_indevs[LVGL_POINTER_MAX_SLOTS];
    memcpy(slot_indevs, pool->slot_indev, sizeof(lv_indev_t*) * slot_count);

    // Every LvglDeviceContext wrapper points at the same pool; null `context` out on all of them
    // before deleting any (LvglDeviceContext's destructor frees `context`, and pool must stay
    // valid for every wrapper's own delete to run safely) - then free the pool exactly once
    // ourselves at the end.
    for (uint8_t i = 0; i < slot_count; i++) {
        lv_indev_t* slot_indev = slot_indevs[i];
        if (slot_indev == NULL) continue;
        if (default_pointer_indev == slot_indev) {
            default_pointer_indev = NULL;
        }
        struct LvglDeviceContext* slot_wrapper = (struct LvglDeviceContext*)lv_indev_get_driver_data(slot_indev);
        slot_wrapper->context = NULL;
        lv_indev_delete(slot_indev);
        delete slot_wrapper;
    }
    delete pool;
}
