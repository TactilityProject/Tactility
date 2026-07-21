#include <lilygo/drivers/trackball.h>
#include <lilygo/drivers/tdeck_trackball.h>

#include <Tactility/Assets.h>

#include <tactility/device.h>
#include <tactility/log.h>


constexpr auto* TAG = "Trackball";

namespace trackball {

static lv_indev_t* g_indev = nullptr;
static Device* g_device = nullptr;
static bool g_enabled = true;
static Mode g_mode = Mode::Encoder;
static uint8_t g_encoderSensitivity = 1;
static uint8_t g_pointerSensitivity = 10;

// Pointer mode cursor position (screen-relative)
static int32_t g_cursorX = 160;
static int32_t g_cursorY = 120;

static lv_obj_t* g_cursor = nullptr;

// Screen dimensions (T-Deck: 320x240)
static constexpr int32_t SCREEN_WIDTH = 320;
static constexpr int32_t SCREEN_HEIGHT = 240;
static constexpr int32_t CURSOR_SIZE = 16;

static inline int32_t clamp(int32_t val, int32_t minVal, int32_t maxVal) {
    if (val < minVal) return minVal;
    if (val > maxVal) return maxVal;
    return val;
}

// Note: must be called from the LVGL thread (main thread), same as the setters below.
static void read_cb(lv_indev_t* /*indev*/, lv_indev_data_t* data) {
    // Always drain accumulated movement so it doesn't jump on re-enable, but discard it while disabled.
    int32_t dx = 0;
    int32_t dy = 0;
    tdeck_trackball_read_delta(g_device, &dx, &dy);
    if (!g_enabled) {
        dx = 0;
        dy = 0;
    }

    if (g_mode == Mode::Encoder) {
        int32_t ticks = (dx + dy) * static_cast<int32_t>(g_encoderSensitivity);
        data->enc_diff = static_cast<int16_t>(clamp(ticks, INT16_MIN, INT16_MAX));
        if (ticks != 0) {
            lv_display_trigger_activity(nullptr);
        }
    } else {
        g_cursorX = clamp(g_cursorX + dx * static_cast<int32_t>(g_pointerSensitivity), 0, SCREEN_WIDTH - CURSOR_SIZE - 1);
        g_cursorY = clamp(g_cursorY + dy * static_cast<int32_t>(g_pointerSensitivity), 0, SCREEN_HEIGHT - CURSOR_SIZE - 1);
        data->point.x = static_cast<int16_t>(g_cursorX);
        data->point.y = static_cast<int16_t>(g_cursorY);
    }

    bool pressed = false;
    if (g_enabled) {
        tdeck_trackball_get_button_pressed(g_device, &pressed);
    }
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;

    if (pressed) {
        lv_display_trigger_activity(nullptr);
    }
}

lv_indev_t* init() {
    if (g_indev != nullptr) {
        LOG_W(TAG, "Already initialized");
        return g_indev;
    }

    g_device = device_find_first_active_by_type(&TDECK_TRACKBALL_TYPE);
    if (g_device == nullptr) {
        LOG_E(TAG, "tdeck_trackball kernel device not found or not started");
        return nullptr;
    }

    g_cursorX = SCREEN_WIDTH / 2;
    g_cursorY = SCREEN_HEIGHT / 2;

    g_indev = lv_indev_create();
    if (g_indev == nullptr) {
        LOG_E(TAG, "Failed to register LVGL input device");
        g_device = nullptr;
        return nullptr;
    }

    lv_indev_set_type(g_indev, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_read_cb(g_indev, read_cb);
    LOG_I(TAG, "Initialized");

    return g_indev;
}

// Create cursor for pointer mode
static void createCursor() {
    if (g_cursor != nullptr || g_indev == nullptr) return;

    g_cursor = lv_image_create(lv_layer_sys());
    if (g_cursor != nullptr) {
        lv_obj_remove_flag(g_cursor, LV_OBJ_FLAG_CLICKABLE);
        lv_image_set_src(g_cursor, TT_ASSETS_UI_CURSOR);
        lv_indev_set_cursor(g_indev, g_cursor);
        LOG_D(TAG, "Cursor created");
    }
}

// Destroy cursor when switching back to encoder mode
static void destroyCursor() {
    if (g_cursor == nullptr) return;

    // Delete the cursor object - this automatically detaches it from the indev
    lv_obj_delete(g_cursor);
    g_cursor = nullptr;
    LOG_D(TAG, "Cursor destroyed");
}

void deinit() {
    if (g_indev == nullptr) return;

    destroyCursor();

    lv_indev_delete(g_indev);
    g_indev = nullptr;
    g_device = nullptr;

    g_mode = Mode::Encoder;
    g_enabled = true;
    LOG_I(TAG, "Deinitialized");
}

void setEncoderSensitivity(uint8_t sensitivity) {
    if (sensitivity > 0) {
        g_encoderSensitivity = sensitivity;
        LOG_D(TAG, "Encoder sensitivity set to %d", sensitivity);
    }
}

void setPointerSensitivity(uint8_t sensitivity) {
    if (sensitivity > 0) {
        g_pointerSensitivity = sensitivity;
        LOG_D(TAG, "Pointer sensitivity set to %d", sensitivity);
    }
}

void setEnabled(bool enabled) {
    g_enabled = enabled;

    if (g_cursor != nullptr) {
        if (enabled) {
            lv_obj_clear_flag(g_cursor, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(g_cursor, LV_OBJ_FLAG_HIDDEN);
        }
    }

    LOG_I(TAG, "%s", enabled ? "Enabled" : "Disabled");
}

void setMode(Mode mode) {
    if (g_indev == nullptr) {
        LOG_W(TAG, "Cannot set mode - not initialized");
        return;
    }

    if (g_mode == mode) {
        return;
    }

    g_mode = mode;

    if (mode == Mode::Pointer) {
        lv_indev_set_type(g_indev, LV_INDEV_TYPE_POINTER);
        createCursor();
        if (!g_enabled && g_cursor != nullptr) {
            lv_obj_add_flag(g_cursor, LV_OBJ_FLAG_HIDDEN);
        }
        g_cursorX = SCREEN_WIDTH / 2;
        g_cursorY = SCREEN_HEIGHT / 2;
        LOG_I(TAG, "Switched to Pointer mode");
    } else {
        destroyCursor();
        lv_indev_set_type(g_indev, LV_INDEV_TYPE_ENCODER);
        LOG_I(TAG, "Switched to Encoder mode");
    }
}

Mode getMode() {
    return g_mode;
}

}
